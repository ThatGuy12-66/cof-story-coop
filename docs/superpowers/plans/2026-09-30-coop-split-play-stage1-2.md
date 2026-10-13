# Split play, stages 1–2: retail layer and shared story items — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Both players carry every story item either of them finds, and lose it together when it is used up, using the retail game's own inventory code. Story items also survive respawns, which fixes the key soft-lock even when playing solo.

**Architecture:** Three new units go into the Xash engine's opt-in story co-op layer:
- **Pure descriptor reader:** `sv_cof_descriptor.h`.
- **Retail game layer:** `sv_cof_retail.c`. It reads inventory slots, gives and removes items through the game's own paths, and sends the game's user messages.
- **Item ledger:** a pure state machine (`sv_cof_share_state.h`) plus engine glue (`sv_cof_share.c`). The glue diffs inventories every frame and around every game client command, then reconciles each player toward the party's set.

Stage 1 ends in a live check of the retail paths with a developer probe command. Stage 2 builds the ledger on top of it.

**Tech Stack:**
- C (MSVC x86 via the existing waf build) and Python 3.13 unittest.
- Native offline harnesses compiled with VS 2022 BuildTools.
- The game engine is Xash3D FWGS with the Cry of Fear Enhanced patch stack.

**Spec:** `docs/superpowers/specs/2026-09-30-coop-split-play-design.md` (v2). Stages 1–2 of its build order. The research behind it is in `docs/research/2026-09-30-hl-inventory-re.md`. **Stages 3–4 (exit hold, warp, doors; shared codes, notes and objectives) get their own plan after Task 3's live results**, because the spec makes later rules depend on those results.

## Global Constraints

- **Retail game untouched:** no map, `hl.dll` or `client.dll` changes. All behavior is inside the engine's opt-in story layer: `cof_story_coop 1`, with `SV_CoF_StoryCoopEnabled()` true. That function also requires the verified retail DLL (SHA-256 `0036B91C…0E63`).
- **Game paths only:** never write an inventory slot directly. The client keeps its own copy and would desync. Give through `SV_CoF_StoryGiveItemEx`, which runs the game's pickup. Remove through the game's `inventorydeletenamed <path> 0` client command.
- **Retail facts in one place:** offsets and message names live only in `sv_cof_retail.c`.
- **Story items** are descriptors directly under `inventoryitems/`. Weapons, ammo and costumes are never shared.
- **Log format:** every log line starts `cofstory phase=`. The phases this plan adds are `item-found`, `item-used`, `item-dropped`, `item-delivered`, `item-removed`, `item-deferred` and `probe`.
- **Code style:** Xash style. Tabs, spaces inside parentheses (`if( x )`), `//` comments that say why, and LF line endings in new engine files.
- **Two repositories:**
  - The engine is `upstream/cof-enhanced/xash3d-fwgs-4857b389e6ba32ddaa68582aedcbc950c138f46a`, branch `story-coop`; called `$ENGINE` below.
  - Tests, tools and docs are the project root, branch `main`.
  - Every commit message ends with:
    ```
    Co-Authored-By: Claude Opus 5.5 (1M context) <noreply@anthropic.com>
    Claude-Session: https://claude.ai/code/session_018mJcNynXJSSzUadjrVgU5W
    ```
- **Running tests.** Run in PowerShell from the project root:
  ```powershell
  Remove-Item Env:NoDefaultCurrentDirectoryInExePath -ErrorAction SilentlyContinue
  $env:PATH = "$env:PATH;C:\Program Files (x86)\Microsoft Visual Studio\Installer"
  & python -m unittest <module> -v
  ```
  Bare `python` is intercepted by a hook, so use this interpreter path.
- **Building:** `& cmd.exe /d /c tools\build_enhanced.cmd`. The output is `$ENGINE\build-local-20260927\engine\xash.dll`.
- **The live game:**
  - Tell the user before launching the game.
  - Never kill or restart their session.
  - Deploy only while no `CoFLaunchApp.exe` or `cof.exe` is running.
  - Never print passwords: RCON output from `tools/enhanced_local_control.py` is already redacted.

## Review Focus

These are the failure modes a player is most likely to hit that the spec implies but does not spell out. Each has a pinned test in the task that owns the code.

1. **A delivery lands after its item was used up elsewhere.** It must be taken back, not re-added to the party. (Task 4, "late delivery" scenario.)
2. **A player picks something up and then sends any command in the same packet.** The pickup must still count as found. (Task 5, "unseen pickup" scenario: `CommandBegin` refreshes the snapshot first.)
3. **A partner is mid level change** (connected, not yet spawned) when an item is found. It is delivered after sign-on, not lost. (Task 5.)
4. **A respawn wipe** (`CBasePlayer::Spawn`) must never read as "used up", or one death would strip the partner too. (Tasks 4 and 5.)
5. **A descriptor that could inject a console command** (quotes, `;`) or a path escape (`..`) is refused before it reaches `inventorydeletenamed` or the filesystem. (Task 2.)

---

## File structure

| File | Repo | Responsibility |
|---|---|---|
| `$ENGINE/engine/server/sv_cof_descriptor.h` | engine | Pure: read `"key" { data "value" }` descriptors; identify story items |
| `$ENGINE/engine/server/sv_cof_retail.h` / `.c` | engine | Retail facts: slots, free slots, give, remove, item names, ProFont, ScreenFade, `cof_story_probe` |
| `$ENGINE/engine/server/sv_game.c` | engine | + `SV_CoF_SendUserMessage` (send a DLL-registered user message by name) |
| `$ENGINE/engine/server/server.h` | engine | + declaration of `SV_CoF_SendUserMessage` |
| `$ENGINE/engine/server/sv_cof_share_state.h` | engine | Pure ledger: set, diffs, plan, events |
| `$ENGINE/engine/server/sv_cof_share.h` / `.c` | engine | Ledger glue: frame, command hooks, reset, `cof_story_ledger` |
| `$ENGINE/engine/server/sv_client.c` | engine | + command hooks around the game's `ClientCommand` |
| `$ENGINE/engine/server/sv_cof_story.c` | engine | + init, frame and shutdown wiring |
| `$ENGINE/engine/server/sv_save.c` | engine | + ledger reset at session boundaries |
| `tests/native.py` | project | Compile and run one native harness (shared helper) |
| `tests/engine/cof_descriptor_test.c`, `tests/test_enhanced_descriptor.py` | project | Descriptor reader tests |
| `tests/engine/cof_retail_harness.c`, `tests/test_enhanced_retail.py` | project | Retail layer against engine doubles |
| `tests/engine/cof_share_state_test.c`, `tests/test_enhanced_share_state.py` | project | Ledger rules |
| `tests/engine/cof_share_harness.c`, `tests/test_enhanced_share.py` | project | Ledger glue against engine doubles |
| `tools/story_run_report.py`, `tests/test_story_run_report.py` | project | Count and warn on the new phases |

---

### Task 1: Native test helper and descriptor reader

**Files:**
- Create: `tests/native.py`
- Create: `tests/engine/cof_descriptor_test.c`
- Create: `tests/test_enhanced_descriptor.py`
- Create: `$ENGINE/engine/server/sv_cof_descriptor.h`

**Interfaces:**
- Produces:
  - `tests.native.ENGINE_SERVER` (Path to `$ENGINE/engine/server`).
  - `tests.native.strip_includes(text) -> str`.
  - `tests.native.run_native(case, harness_filename, files: dict[str, str]) -> str` returns the harness's stdout, fails the test on a non-zero exit, and skips without a compiler.
  - In C, `static int CoF_DescriptorValue( const char *text, const char *key, char *out, size_t size )` returns 1 and fills `out`, or returns 0 with `out` empty.
  - In C, `static int CoF_IsStoryItem( const char *descriptor )`.

- [ ] **Step 1: Write the shared native helper**

`tests/native.py`:
```python
"""Compile and run one native C harness offline: MSVC x86 on Windows, cc elsewhere.

Production sources are passed in as text, usually with their #include lines
stripped, so a harness supplies its own engine doubles. Nothing here needs the
game, a game DLL or the network.
"""
import os
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
ENGINE_SERVER = ROOT / 'upstream/cof-enhanced/xash3d-fwgs-4857b389e6ba32ddaa68582aedcbc950c138f46a/engine/server'
VCVARS = Path(r'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat')


def strip_includes(text: str) -> str:
    return '\n'.join(line for line in text.splitlines() if not line.lstrip().startswith('#include'))


def run_native(case: unittest.TestCase, harness: str, files: dict, timeout: int = 120) -> str:
    if not VCVARS.exists() and not shutil.which('cc'):
        case.skipTest('a C compiler is required for the offline engine harness')
    with tempfile.TemporaryDirectory(prefix='cof-native-') as tmp:
        work = Path(tmp)
        for name, text in files.items():
            (work / name).write_text(text)
        shutil.copyfile(ROOT / 'tests/engine' / harness, work / 'harness.c')
        if os.name == 'nt':
            (work / 'build.cmd').write_text(
                f'@call "{VCVARS}" amd64_x86 >nul\n'
                '@cl /nologo /W4 /WX /wd4100 /wd4505 /D_CRT_SECURE_NO_WARNINGS /TC harness.c /Fe:native-test.exe\n'
                '@if errorlevel 1 exit /b %errorlevel%\n'
                '@.\\native-test.exe\n')
            command = ['cmd.exe', '/d', '/c', str(work / 'build.cmd')]
        else:
            command = ['sh', '-c', 'cc -std=c99 -Wall -Wextra -Werror -Wno-unused-parameter -Wno-unused-function '
                       'harness.c -o native-test && ./native-test']
        result = subprocess.run(command, cwd=work, capture_output=True, text=True, timeout=timeout)
        case.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        return result.stdout
```

- [ ] **Step 2: Write the failing descriptor test**

`tests/engine/cof_descriptor_test.c`:
```c
/* The inventory descriptor reader, driven directly. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "sv_cof_descriptor.h"

int main( void )
{
	/* A real descriptor, CRLF as shipped (inventoryitems/apartmentkey.txt). */
	static const char key[] =
		"\"map_name\"\r\n{\r\n\tdata\t\"apartmentkey\"\r\n}\r\n\r\n\r\n"
		"\"nice_name\"\r\n{\r\n\tdata\t\"Apartment key\"\r\n}\r\n\r\n\r\n"
		"\"classname\"\r\n{\r\n\tdata\t\"item_key\"\r\n}\r\n";
	static const char tricky[] = "\"a\"\n{\n data \"classname\"\n}\n\"classname\"\n{\n data \"item_key\"\n}\n";
	char out[64], tiny[4];

	assert( CoF_DescriptorValue( key, "nice_name", out, sizeof( out )) && !strcmp( out, "Apartment key" ));
	assert( CoF_DescriptorValue( key, "classname", out, sizeof( out )) && !strcmp( out, "item_key" ));
	assert( !CoF_DescriptorValue( key, "droppable", out, sizeof( out )) && !out[0] );
	/* a key name that also appears as a value is matched only as a key */
	assert( CoF_DescriptorValue( tricky, "classname", out, sizeof( out )) && !strcmp( out, "item_key" ));
	/* truncation keeps a terminator */
	assert( CoF_DescriptorValue( key, "nice_name", tiny, sizeof( tiny )) && !strcmp( tiny, "Apa" ));
	/* an unterminated value is no value */
	assert( !CoF_DescriptorValue( "\"k\" { data \"open", "k", out, sizeof( out )) && !out[0] );

	/* story items: directly under inventoryitems/, case kept as shipped */
	assert( CoF_IsStoryItem( "inventoryitems/apartmentkey.txt" ));
	assert( CoF_IsStoryItem( "inventoryitems/Videotape.txt" ));
	assert( !CoF_IsStoryItem( "inventoryitems/weapons/weapon_mobile.txt" ));
	assert( !CoF_IsStoryItem( "inventoryitems/ammo/glock.txt" ));
	assert( !CoF_IsStoryItem( "inventoryitems/.txt" ));
	assert( !CoF_IsStoryItem( "" ) && !CoF_IsStoryItem( "weapon_glock" ) && !CoF_IsStoryItem( NULL ));
	puts( "descriptor scenarios passed" );
	return 0;
}
```

`tests/test_enhanced_descriptor.py`:
```python
"""Inventory descriptor reader and story-item test, run natively offline."""
import unittest

from tests.native import ENGINE_SERVER, run_native


class DescriptorTests(unittest.TestCase):
    def test_descriptor_reader(self):
        out = run_native(self, 'cof_descriptor_test.c', {
            'sv_cof_descriptor.h': (ENGINE_SERVER / 'sv_cof_descriptor.h').read_text(),
        })
        self.assertIn('descriptor scenarios passed', out)


if __name__ == '__main__':
    unittest.main()
```

- [ ] **Step 3: Run it to see it fail**

Run: `& python -m unittest tests.test_enhanced_descriptor -v` (with the env setup from Global Constraints).
Expected: ERROR with `FileNotFoundError` for `sv_cof_descriptor.h`.

- [ ] **Step 4: Write the descriptor reader**

`$ENGINE/engine/server/sv_cof_descriptor.h`:
```c
/* Cry of Fear inventory descriptors (cryoffear/inventoryitems/*.txt).
 * Pure helpers, no engine types. GPL-3.0-or-later.
 * A descriptor is a list of blocks:  "key" { data "value" }
 * Story items are the descriptors directly under inventoryitems/; weapons/,
 * ammo/ and outofit/ hold weapons, ammo and costumes. */
#ifndef SV_COF_DESCRIPTOR_H
#define SV_COF_DESCRIPTOR_H
#include <string.h>

static const char *CoF_DescSkipSpace( const char *p )
{
	while( *p == ' ' || *p == '\t' || *p == '\r' || *p == '\n' ) p++;
	return p;
}

// A quoted token at p (after spaces) into out, truncated to fit. Returns the
// position after its closing quote, or NULL without a complete quoted token.
static const char *CoF_DescQuoted( const char *p, char *out, size_t size )
{
	size_t n = 0;
	p = CoF_DescSkipSpace( p );
	if( *p != '"' ) return NULL;
	for( p++; *p && *p != '"'; p++ )
		if( n + 1 < size ) out[n++] = *p;
	out[n] = 0;
	return *p == '"' ? p + 1 : NULL;
}

// The value of "key" { data "value" }: 1 with out filled, or 0 with out empty.
static int CoF_DescriptorValue( const char *text, const char *key, char *out, size_t size )
{
	char quoted[80];
	size_t length = strlen( key );
	const char *p;
	if( !size ) return 0;
	out[0] = 0;
	if( length + 3 > sizeof( quoted )) return 0;
	quoted[0] = '"';
	memcpy( quoted + 1, key, length );
	quoted[length + 1] = '"';
	quoted[length + 2] = 0;
	for( p = strstr( text, quoted ); p; p = strstr( p + 1, quoted ))
	{
		const char *q = CoF_DescSkipSpace( p + length + 2 );
		if( *q != '{' ) continue; // the text appeared as a value, not a key
		q = CoF_DescSkipSpace( q + 1 );
		if( strncmp( q, "data", 4 )) continue;
		if( CoF_DescQuoted( q + 4, out, size )) return 1;
		out[0] = 0;
	}
	return 0;
}

static int CoF_IsStoryItem( const char *descriptor )
{
	static const char prefix[] = "inventoryitems/";
	const char *name;
	size_t length;
	if( !descriptor || strncmp( descriptor, prefix, sizeof( prefix ) - 1 )) return 0;
	name = descriptor + sizeof( prefix ) - 1;
	length = strlen( name );
	if( length <= 4 || strchr( name, '/' ) || strchr( name, '\\' )) return 0;
	return !strcmp( name + length - 4, ".txt" );
}
#endif
```

- [ ] **Step 5: Run it to see it pass**

Run: `& python -m unittest tests.test_enhanced_descriptor -v`
Expected: `test_descriptor_reader ... ok`

- [ ] **Step 6: Commit both repos**

```bash
git -C "$ENGINE" add engine/server/sv_cof_descriptor.h
git -C "$ENGINE" commit -m "Add Cry of Fear inventory descriptor reader" -m "Co-Authored-By: Claude Opus 5.5 (1M context) <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_018mJcNynXJSSzUadjrVgU5W"
git add tests/native.py tests/engine/cof_descriptor_test.c tests/test_enhanced_descriptor.py
git commit -m "Test the descriptor reader; shared native harness helper" -m "Co-Authored-By: Claude Opus 5.5 (1M context) <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_018mJcNynXJSSzUadjrVgU5W"
```

---

### Task 2: User-message sender and the retail layer

**Files:**
- Modify: `$ENGINE/engine/server/sv_game.c` (insert after the closing brace of `pfnWriteString`, which is just before the `pfnWriteEntity` comment block)
- Modify: `$ENGINE/engine/server/server.h` (after the `SV_CoF_MapExitModels` declaration)
- Create: `$ENGINE/engine/server/sv_cof_retail.h`
- Create: `$ENGINE/engine/server/sv_cof_retail.c`
- Modify: `$ENGINE/engine/server/sv_cof_story.c` (`SV_CoF_StoryInit`)
- Create: `tests/engine/cof_retail_harness.c`
- Create: `tests/test_enhanced_retail.py`

**Interfaces:**
- Consumes (from Task 1): `CoF_DescriptorValue`.
- Consumes (existing):
  - `SV_CoF_StoryEntityPrivate( edict_t *, size_t )` and `SV_CoF_StoryGiveItemEx( sv_client_t *, const char *classname, const char *message )`, both in `sv_cof_cheats.h`.
  - `SV_CoF_StoryCoopEnabled()`.
- Produces:
  - `qboolean SV_CoF_SendUserMessage( edict_t *to, const char *name, const byte *payload, int size, qboolean reliable )`.
  - Everything in `sv_cof_retail.h` below, including `COF_RETAIL_SLOTS` (6) and `COF_RETAIL_ITEM` (64).
  - The server console command `cof_story_probe`.

- [ ] **Step 1: Write the failing retail harness**

`tests/engine/cof_retail_harness.c`:
```c
/* Offline doubles for sv_cof_retail.c: no game DLL, network or game process. */
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef unsigned char byte;
typedef int qboolean;
typedef int string_t;
typedef long long fs_offset_t;
#define true 1
#define false 0
#define cs_spawned 4
#define S_USAGE "Usage: "
#define bound( lo, x, hi ) (( x ) < ( lo ) ? ( lo ) : (( x ) > ( hi ) ? ( hi ) : ( x )))
#define Q_strcmp strcmp
#define Q_strncmp strncmp
#define Q_strlen strlen
#define Q_strstr strstr
#define Q_atoi atoi
#define Q_snprintf snprintf
static size_t Q_strncpy( char *dst, const char *src, size_t size )
{
	size_t n = strlen( src );
	if( !size ) return n;
	if( n >= size ) { memcpy( dst, src, size - 1 ); dst[size - 1] = 0; }
	else memcpy( dst, src, n + 1 );
	return n;
}
typedef struct { float health; int deadflag; } entvars_t;
typedef struct edict_s { int free; void *pvPrivateData; entvars_t v; } edict_t;
typedef struct { int state, userid; unsigned int flags; char name[32]; edict_t *edict; } sv_client_t;
typedef void ( *xcommand_t )( void );
static struct { int maxclients; sv_client_t *clients; } svs;
static struct { struct { void ( *pfnClientCommand )( edict_t *ent ); } dllFuncs; } svgame;
static edict_t edicts[3];
static sv_client_t clients[2];
static byte private_data[2][0x2300];
static const char *strings[] = { "", "inventoryitems/hallkey.txt", "inventoryitems/weapons/weapon_mobile.txt", "inventoryitems/fuse.txt" };
static int gate = 1, cheats = 0, give_calls, give_result = 1, argc;
static const char *argv[8];
static char last_command[256], last_give_class[64], last_give_message[128], last_msg_name[32], console[8192];
static edict_t *last_command_edict;
static byte last_msg[256];
static int last_msg_size, last_msg_reliable;
static size_t console_length;
static xcommand_t probe;

static qboolean SV_IsValidEdict( const edict_t *e ) { return e && !e->free; }
static byte *SV_CoF_StoryEntityPrivate( edict_t *e, size_t need ) { return e && e->pvPrivateData && need <= 0x2300 ? (byte *)e->pvPrivateData : NULL; }
static const char *SV_GetString( string_t s ) { assert( s >= 0 && s < 4 ); return strings[s]; }
static qboolean SV_CoF_StoryCoopEnabled( void ) { return gate; }
static int Cvar_VariableInteger( const char *name ) { return !strcmp( name, "sv_cheats" ) ? cheats : 0; }
static byte *FS_LoadFile( const char *path, fs_offset_t *size, qboolean gamedironly )
{
	static const char hallkey[] = "\"map_name\"\r\n{\r\n\tdata\t\"hallkey\"\r\n}\r\n\"nice_name\"\r\n{\r\n\tdata\t\"Hall key\"\r\n}\r\n\"classname\"\r\n{\r\n\tdata\t\"item_key\"\r\n}\r\n";
	static const char mobile[] = "\"classname\"\r\n{\r\n\tdata\t\"weapon_mobile\"\r\n}\r\n";
	const char *text = !strcmp( path, "inventoryitems/hallkey.txt" ) ? hallkey : !strcmp( path, "inventoryitems/weapons/weapon_mobile.txt" ) ? mobile : NULL;
	char *copy;
	if( !text ) return NULL;
	copy = malloc( strlen( text ) + 1 );
	strcpy( copy, text );
	if( size ) *size = (fs_offset_t)strlen( text );
	return (byte *)copy;
}
static void Mem_Free( void *p ) { free( p ); }
static qboolean SV_CoF_StoryGiveItemEx( sv_client_t *cl, const char *classname, const char *message )
{
	give_calls++;
	Q_strncpy( last_give_class, classname, sizeof( last_give_class ));
	Q_strncpy( last_give_message, message, sizeof( last_give_message ));
	return give_result;
}
static void Cmd_TokenizeString( const char *text ) { Q_strncpy( last_command, text, sizeof( last_command )); }
static void ClientCommand( edict_t *e ) { last_command_edict = e; }
static qboolean SV_CoF_SendUserMessage( edict_t *to, const char *name, const byte *payload, int size, qboolean reliable )
{
	Q_strncpy( last_msg_name, name, sizeof( last_msg_name ));
	memcpy( last_msg, payload, size );
	last_msg_size = size;
	last_msg_reliable = reliable;
	return true;
}
static void Con_Printf( const char *fmt, ... )
{
	va_list args;
	va_start( args, fmt );
	console_length += (size_t)vsnprintf( console + console_length, sizeof( console ) - console_length, fmt, args );
	va_end( args );
}
static int Cmd_Argc( void ) { return argc; }
static const char *Cmd_Argv( int i ) { return i < argc ? argv[i] : ""; }
static void Cmd_AddCommand( const char *name, xcommand_t f, const char *desc ) { probe = f; }

#include "sv_cof_descriptor.h"
#include "sv_cof_retail.h"
#include "retail_under_test.c"

int main( void )
{
	char slots[COF_RETAIL_SLOTS][COF_RETAIL_ITEM], nice[64];
	string_t id;
	int i;
	static const byte fade[10] = { 0x00, 0x08, 0x99, 0x01, 0x00, 0x00, 0, 0, 0, 255 };

	svs.maxclients = 2;
	svs.clients = clients;
	svgame.dllFuncs.pfnClientCommand = ClientCommand;
	for( i = 0; i < 2; i++ )
	{
		clients[i].state = cs_spawned;
		clients[i].userid = i + 1;
		clients[i].edict = &edicts[i + 1];
		edicts[i + 1].pvPrivateData = private_data[i];
	}
	SV_CoF_RetailInit();
	assert( probe );

	/* slots: usable slots 1..6 only; the game never uses slot 0 */
	id = 2; memcpy( private_data[0] + 0x21FC, &id, sizeof( id ));
	id = 1; memcpy( private_data[0] + 0x21FC + 1 * sizeof( id ), &id, sizeof( id ));
	id = 3; memcpy( private_data[0] + 0x21FC + 6 * sizeof( id ), &id, sizeof( id ));
	assert( SV_CoF_RetailReadSlots( &clients[0], slots ));
	assert( !strcmp( slots[0], "inventoryitems/hallkey.txt" ) && !slots[1][0] && !strcmp( slots[5], "inventoryitems/fuse.txt" ));
	assert( SV_CoF_RetailFreeSlots( &clients[0] ) == 4 );
	private_data[0][0x1E9C] = 1; /* three-slot mode: only slots 1..3 count */
	assert( SV_CoF_RetailFreeSlots( &clients[0] ) == 2 );
	private_data[0][0x1E9C] = 0;
	clients[1].state = 2; /* not spawned: nothing is read */
	assert( !SV_CoF_RetailReadSlots( &clients[1], slots ) && SV_CoF_RetailFreeSlots( &clients[1] ) == -1 );
	clients[1].state = cs_spawned;

	/* give: the classname comes from the descriptor, through the game's pickup */
	assert( SV_CoF_RetailGiveItem( &clients[1], "inventoryitems/hallkey.txt" ));
	assert( !strcmp( last_give_class, "item_key" ) && !strcmp( last_give_message, "inventoryitems/hallkey.txt" ));
	give_calls = 0;
	assert( !SV_CoF_RetailGiveItem( &clients[1], "inventoryitems/weapons/weapon_mobile.txt" )); /* not a story item class */
	assert( !SV_CoF_RetailGiveItem( &clients[1], "inventoryitems/fuse.txt" )); /* no descriptor file */
	assert( !SV_CoF_RetailGiveItem( &clients[1], "inventoryitems/../config.cfg" ));
	assert( give_calls == 0 );

	/* remove: the game's own ClientCommand, run as that player */
	assert( SV_CoF_RetailRemoveItem( &clients[1], "inventoryitems/hallkey.txt" ));
	assert( !strcmp( last_command, "inventorydeletenamed \"inventoryitems/hallkey.txt\" 0" ) && last_command_edict == clients[1].edict );
	last_command[0] = 0;
	assert( !SV_CoF_RetailRemoveItem( &clients[1], "x\";quit;\"" ) && !last_command[0] );

	/* names */
	assert( SV_CoF_RetailItemName( "inventoryitems/hallkey.txt", nice, sizeof( nice )) && !strcmp( nice, "Hall key" ));
	assert( !SV_CoF_RetailItemName( "inventoryitems/fuse.txt", nice, sizeof( nice )) && !strcmp( nice, "item" ));

	/* ProFont: a colour byte, then the text with its terminator */
	assert( SV_CoF_RetailProFont( &clients[0], 2, "hi", false ));
	assert( !strcmp( last_msg_name, "ProFont" ) && last_msg_size == 4 && !memcmp( last_msg, "\x02hi", 4 ) && !last_msg_reliable );
	assert( !SV_CoF_RetailProFont( &clients[0], 2, "a DEADNOTIFY line", true )); /* the client would swap in its respawn prompt */
	assert( !SV_CoF_RetailProFont( &clients[0], 9, "hi", true ));

	/* ScreenFade: three x4096 shorts, little-endian, then rgba */
	assert( SV_CoF_RetailScreenFade( &clients[0], 0.5f, 0.1f, 0, 0, 0, 0, 255 ));
	assert( !strcmp( last_msg_name, "ScreenFade" ) && last_msg_size == 10 && !memcmp( last_msg, fade, 10 ) && last_msg_reliable );

	/* probe: give and remove need sv_cheats; slots and messages do not */
	argc = 4; argv[0] = "cof_story_probe"; argv[1] = "give"; argv[2] = "2"; argv[3] = "inventoryitems/hallkey.txt";
	give_calls = 0; probe();
	assert( give_calls == 0 && strstr( console, "what=give result=0 reason=needs-sv_cheats" ));
	cheats = 1; probe();
	assert( give_calls == 1 && strstr( console, "cofstory phase=probe what=give userid=2 result=1" ));
	argc = 3; argv[1] = "slots"; argv[2] = "1"; probe();
	assert( strstr( console, "cofstory phase=probe-slot userid=1 index=1 descriptor=\"inventoryitems/hallkey.txt\"" ));
	assert( strstr( console, "what=slots userid=1 result=1 free=4" ));
	argc = 5; argv[1] = "msg"; argv[3] = "4"; argv[4] = "hello"; probe();
	assert( last_msg[0] == 4 && !strcmp( (char *)last_msg + 1, "hello" ));
	argv[2] = "99"; probe();
	assert( strstr( console, "reason=no-such-player" ));
	gate = 0; argv[2] = "1"; probe();
	assert( strstr( console, "reason=story-off" ));
	puts( "retail scenarios passed" );
	return 0;
}
```

`tests/test_enhanced_retail.py`:
```python
"""The retail layer against offline engine doubles: no DLL, game or network."""
import unittest

from tests.native import ENGINE_SERVER, run_native, strip_includes


class RetailTests(unittest.TestCase):
    def test_retail_layer(self):
        out = run_native(self, 'cof_retail_harness.c', {
            'sv_cof_descriptor.h': (ENGINE_SERVER / 'sv_cof_descriptor.h').read_text(),
            'sv_cof_retail.h': (ENGINE_SERVER / 'sv_cof_retail.h').read_text(),
            'retail_under_test.c': strip_includes((ENGINE_SERVER / 'sv_cof_retail.c').read_text()),
        })
        self.assertIn('retail scenarios passed', out)


if __name__ == '__main__':
    unittest.main()
```

- [ ] **Step 2: Run it to see it fail**

Run: `& python -m unittest tests.test_enhanced_retail -v`
Expected: ERROR with `FileNotFoundError` for `sv_cof_retail.h`.

- [ ] **Step 3: Add the user-message sender to the engine**

In `$ENGINE/engine/server/sv_game.c`, insert this directly after the closing brace of `static void GAME_EXPORT pfnWriteString( const char *src )`:
```c
/*
=============
SV_CoF_SendUserMessage

Cry of Fear story co-op: send one of the game DLL's registered user messages,
found by name, to one player - the same calls the DLL itself makes.
=============
*/
qboolean SV_CoF_SendUserMessage( edict_t *to, const char *name, const byte *payload, int size, qboolean reliable )
{
	int i, j;

	if( !SV_IsValidEdict( to ) || !name || size < 0 || ( size && !payload ) || svgame.msg_started )
		return false;

	for( i = 1; i < MAX_USER_MESSAGES && svgame.msg[i].name[0]; i++ )
	{
		if( Q_strcmp( svgame.msg[i].name, name ))
			continue;
		if( svgame.msg[i].size != -1 && svgame.msg[i].size != size )
			return false;
		pfnMessageBegin( reliable ? MSG_ONE : MSG_ONE_UNRELIABLE, svgame.msg[i].number, NULL, to );
		for( j = 0; j < size; j++ )
			pfnWriteByte( payload[j] );
		pfnMessageEnd();
		return true;
	}
	return false;
}
```

In `$ENGINE/engine/server/server.h`, after the line `int SV_CoF_MapExitModels( const char *map, const char *destination, const char *landmark, int *models, int max );`, add:
```c
qboolean SV_CoF_SendUserMessage( edict_t *to, const char *name, const byte *payload, int size, qboolean reliable );
```

- [ ] **Step 4: Write the retail layer**

`$ENGINE/engine/server/sv_cof_retail.h`:
```c
/* Opt-in Cry of Fear story co-op: the retail game DLL's inventory, commands
 * and user messages. GPL-3.0-or-later. */
#ifndef SV_COF_RETAIL_H
#define SV_COF_RETAIL_H
#define COF_RETAIL_SLOTS 6 // usable inventory slots 1..6 (the game never uses index 0)
#define COF_RETAIL_ITEM 64 // descriptor path buffer

void SV_CoF_RetailInit( void );
// The descriptors in usable slots 1..6 ("" when empty); false without a verified, spawned player.
qboolean SV_CoF_RetailReadSlots( sv_client_t *cl, char slots[COF_RETAIL_SLOTS][COF_RETAIL_ITEM] );
// Empty usable slots (only 1..3 count in the game's three-slot mode), or -1 without a verified player.
int SV_CoF_RetailFreeSlots( sv_client_t *cl );
// Hand a story item over through the game's own pickup (KeyitemUse -> AddInventoryItem).
qboolean SV_CoF_RetailGiveItem( sv_client_t *cl, const char *descriptor );
// Remove one story item through the game's own inventorydeletenamed command.
qboolean SV_CoF_RetailRemoveItem( sv_client_t *cl, const char *descriptor );
// The descriptor's nice_name ("Hall key"); "item" and false when unknown.
qboolean SV_CoF_RetailItemName( const char *descriptor, char *out, size_t size );
// A line in the game's own message strip: colour 0/3 yellow, 1 azure, 2/6 red, 4 green, 5 grey.
qboolean SV_CoF_RetailProFont( sv_client_t *cl, int colour, const char *text, qboolean reliable );
// The game's ScreenFade: seconds go out as x4096 shorts; flags 0 fades in from the colour.
qboolean SV_CoF_RetailScreenFade( sv_client_t *cl, float duration, float hold, int flags, int r, int g, int b, int a );
#endif
```

`$ENGINE/engine/server/sv_cof_retail.c`:
```c
/* Cry of Fear story co-op: the one place that knows the retail game DLL's
 * inventory layout, commands and user messages. GPL-3.0-or-later.
 * Everything here is behind the retail DLL hash gate; the layout and paths are
 * documented in docs/research/2026-09-30-hl-inventory-re.md (project repo). */
#include "common.h"
#include "server.h"
#include "sv_cof_cheats.h"
#include "sv_cof_story.h"
#include "sv_cof_descriptor.h"
#include "sv_cof_retail.h"

#define COF_RETAIL_INVENTORY   0x21FC // string_t inventorySlot[7], slots 1..6 used (AddInventoryItem 100DAD40)
#define COF_RETAIL_THREE_SLOTS 0x1E9C // bool m_bThreeSlots: only slots 1..3 usable
#define COF_RETAIL_PLAYER_NEED ( COF_RETAIL_INVENTORY + 7 * sizeof( string_t ))
#define COF_RETAIL_CACHE       128

typedef struct
{
	char descriptor[COF_RETAIL_ITEM];
	char classname[32];
	char nice[COF_RETAIL_ITEM];
	qboolean valid;
} cof_retail_item_t;

static cof_retail_item_t cof_retail_items[COF_RETAIL_CACHE];
static int cof_retail_item_count;

static byte *CoF_RetailPlayer( sv_client_t *cl )
{
	if( !cl || cl->state != cs_spawned || !SV_IsValidEdict( cl->edict )) return NULL;
	return SV_CoF_StoryEntityPrivate( cl->edict, COF_RETAIL_PLAYER_NEED );
}

// Descriptor paths as the game ships them: letters, digits, '_', '-', '.', '/'.
// Anything else is refused before it reaches a console command or a file path.
static qboolean CoF_RetailSafeDescriptor( const char *d )
{
	const char *c;
	if( !d || !d[0] || Q_strlen( d ) >= COF_RETAIL_ITEM || Q_strstr( d, ".." )) return false;
	for( c = d; *c; c++ )
		if( !(( *c >= 'a' && *c <= 'z' ) || ( *c >= 'A' && *c <= 'Z' ) || ( *c >= '0' && *c <= '9' ) ||
			*c == '_' || *c == '-' || *c == '.' || *c == '/' )) return false;
	return true;
}

// The descriptor file's classname and nice_name, loaded once per session.
static const cof_retail_item_t *CoF_RetailItem( const char *descriptor )
{
	cof_retail_item_t *item;
	byte *text;
	fs_offset_t size = 0;
	int i;
	if( !CoF_RetailSafeDescriptor( descriptor )) return NULL;
	for( i = 0; i < cof_retail_item_count; i++ )
		if( !Q_strcmp( cof_retail_items[i].descriptor, descriptor ))
			return cof_retail_items[i].valid ? &cof_retail_items[i] : NULL;
	if( cof_retail_item_count >= COF_RETAIL_CACHE ) return NULL;
	item = &cof_retail_items[cof_retail_item_count++];
	memset( item, 0, sizeof( *item ));
	Q_strncpy( item->descriptor, descriptor, sizeof( item->descriptor ));
	text = FS_LoadFile( descriptor, &size, false ); // zero-terminated by the filesystem
	if( !text ) return NULL;
	item->valid = CoF_DescriptorValue( (const char *)text, "classname", item->classname, sizeof( item->classname )) &&
		!Q_strncmp( item->classname, "item_", 5 );
	if( !CoF_DescriptorValue( (const char *)text, "nice_name", item->nice, sizeof( item->nice )))
		Q_strncpy( item->nice, "item", sizeof( item->nice ));
	Mem_Free( text );
	return item->valid ? item : NULL;
}

qboolean SV_CoF_RetailReadSlots( sv_client_t *cl, char slots[COF_RETAIL_SLOTS][COF_RETAIL_ITEM] )
{
	byte *pv = CoF_RetailPlayer( cl );
	int i;
	if( !pv ) return false;
	for( i = 0; i < COF_RETAIL_SLOTS; i++ )
	{
		string_t id;
		memcpy( &id, pv + COF_RETAIL_INVENTORY + ( i + 1 ) * sizeof( id ), sizeof( id ));
		Q_strncpy( slots[i], id ? SV_GetString( id ) : "", COF_RETAIL_ITEM );
	}
	return true;
}

int SV_CoF_RetailFreeSlots( sv_client_t *cl )
{
	byte *pv = CoF_RetailPlayer( cl );
	int i, usable, count = 0;
	if( !pv ) return -1;
	usable = pv[COF_RETAIL_THREE_SLOTS] ? 3 : COF_RETAIL_SLOTS;
	for( i = 1; i <= usable; i++ )
	{
		string_t id;
		memcpy( &id, pv + COF_RETAIL_INVENTORY + i * sizeof( id ), sizeof( id ));
		if( !id ) count++;
	}
	return count;
}

qboolean SV_CoF_RetailGiveItem( sv_client_t *cl, const char *descriptor )
{
	const cof_retail_item_t *item = CoF_RetailItem( descriptor );
	if( !item || !CoF_RetailPlayer( cl )) return false;
	return SV_CoF_StoryGiveItemEx( cl, item->classname, descriptor );
}

qboolean SV_CoF_RetailRemoveItem( sv_client_t *cl, const char *descriptor )
{
	char text[COF_RETAIL_ITEM + 32];
	if( !CoF_RetailSafeDescriptor( descriptor ) || !CoF_RetailPlayer( cl ) || !svgame.dllFuncs.pfnClientCommand ) return false;
	// The game's own removal: ClientCommand -> DeleteInventoryItemNamed
	// (100DDA70) clears the first exact match and sends Inventory, so the
	// client's copy of the slot stays in sync. A direct slot write would not.
	Q_snprintf( text, sizeof( text ), "inventorydeletenamed \"%s\" 0", descriptor );
	Cmd_TokenizeString( text );
	svgame.dllFuncs.pfnClientCommand( cl->edict );
	return true;
}

qboolean SV_CoF_RetailItemName( const char *descriptor, char *out, size_t size )
{
	const cof_retail_item_t *item = CoF_RetailItem( descriptor );
	Q_strncpy( out, item ? item->nice : "item", size );
	return item != NULL;
}

qboolean SV_CoF_RetailProFont( sv_client_t *cl, int colour, const char *text, qboolean reliable )
{
	byte payload[192]; // the largest GoldSrc user message
	size_t length;
	if( !cl || cl->state != cs_spawned || !text || colour < 0 || colour > 6 ) return false;
	// The client's ProFont hook replaces any line containing DEADNOTIFY with its respawn prompt.
	if( Q_strstr( text, "DEADNOTIFY" )) return false;
	length = Q_strlen( text );
	if( length + 2 > sizeof( payload )) return false;
	payload[0] = (byte)colour;
	memcpy( payload + 1, text, length + 1 );
	return SV_CoF_SendUserMessage( cl->edict, "ProFont", payload, (int)length + 2, reliable );
}

qboolean SV_CoF_RetailScreenFade( sv_client_t *cl, float duration, float hold, int flags, int r, int g, int b, int a )
{
	byte payload[10]; // "ScreenFade" is registered with size 10 (0x10226720)
	int d = (int)( duration * 4096.0f ), h = (int)( hold * 4096.0f );
	if( !cl || cl->state != cs_spawned ) return false;
	d = bound( 0, d, 0xFFFF );
	h = bound( 0, h, 0xFFFF );
	payload[0] = (byte)( d & 0xFF ); payload[1] = (byte)(( d >> 8 ) & 0xFF );
	payload[2] = (byte)( h & 0xFF ); payload[3] = (byte)(( h >> 8 ) & 0xFF );
	payload[4] = (byte)( flags & 0xFF ); payload[5] = (byte)(( flags >> 8 ) & 0xFF );
	payload[6] = (byte)r; payload[7] = (byte)g; payload[8] = (byte)b; payload[9] = (byte)a;
	return SV_CoF_SendUserMessage( cl->edict, "ScreenFade", payload, sizeof( payload ), true );
}

static sv_client_t *CoF_RetailClientByUserid( int userid )
{
	int i;
	for( i = 0; i < svs.maxclients; i++ )
		if( svs.clients[i].state == cs_spawned && svs.clients[i].userid == userid ) return &svs.clients[i];
	return NULL;
}

// Developer probe for the live checks, from the server console or RCON:
// cof_story_probe <slots|give|remove|msg|fade> <userid> [descriptor | colour text]
static void CoF_RetailProbe_f( void )
{
	char what[16], arg[COF_RETAIL_ITEM], text[128];
	sv_client_t *cl;
	int result = 0, i;
	if( Cmd_Argc() < 3 )
	{
		Con_Printf( S_USAGE "cof_story_probe <slots|give|remove|msg|fade> <userid> [descriptor | colour text]\n" );
		return;
	}
	// Copy the arguments first: a removal re-tokenizes the command buffer.
	Q_strncpy( what, Cmd_Argv( 1 ), sizeof( what ));
	cl = CoF_RetailClientByUserid( Q_atoi( Cmd_Argv( 2 )));
	Q_strncpy( arg, Cmd_Argc() > 3 ? Cmd_Argv( 3 ) : "", sizeof( arg ));
	Q_strncpy( text, Cmd_Argc() > 4 ? Cmd_Argv( 4 ) : "", sizeof( text ));
	if( !cl || !SV_CoF_StoryCoopEnabled() )
	{
		Con_Printf( "cofstory phase=probe what=%s result=0 reason=%s\n", what, cl ? "story-off" : "no-such-player" );
		return;
	}
	if( !Q_strcmp( what, "slots" ))
	{
		char slots[COF_RETAIL_SLOTS][COF_RETAIL_ITEM];
		result = SV_CoF_RetailReadSlots( cl, slots );
		for( i = 0; result && i < COF_RETAIL_SLOTS; i++ )
			Con_Printf( "cofstory phase=probe-slot userid=%d index=%d descriptor=\"%s\"\n", cl->userid, i + 1, slots[i] );
		Con_Printf( "cofstory phase=probe what=slots userid=%d result=%d free=%d\n", cl->userid, result, SV_CoF_RetailFreeSlots( cl ));
		return;
	}
	if(( !Q_strcmp( what, "give" ) || !Q_strcmp( what, "remove" )) && !Cvar_VariableInteger( "sv_cheats" ))
	{
		Con_Printf( "cofstory phase=probe what=%s result=0 reason=needs-sv_cheats\n", what );
		return;
	}
	if( !Q_strcmp( what, "give" )) result = SV_CoF_RetailGiveItem( cl, arg );
	else if( !Q_strcmp( what, "remove" )) result = SV_CoF_RetailRemoveItem( cl, arg );
	else if( !Q_strcmp( what, "msg" )) result = SV_CoF_RetailProFont( cl, Q_atoi( arg ), text, true );
	else if( !Q_strcmp( what, "fade" )) result = SV_CoF_RetailScreenFade( cl, 0.5f, 0.1f, 0, 0, 0, 0, 255 );
	Con_Printf( "cofstory phase=probe what=%s userid=%d result=%d\n", what, cl->userid, result );
}

void SV_CoF_RetailInit( void )
{
	Cmd_AddCommand( "cof_story_probe", CoF_RetailProbe_f, "story co-op developer probe: slots, give, remove, msg, fade" );
}
```

In `$ENGINE/engine/server/sv_cof_story.c`, add `#include "sv_cof_retail.h"` after `#include "sv_cof_party.h"`, and make `SV_CoF_StoryInit` register the probe:
```c
void SV_CoF_StoryInit( void )
{
	Cvar_Get( "cof_story_coop", "0", 0, "experimental Cry of Fear story co-op world and party persistence" );
	Cmd_AddCommand( "cof_story_status", CoF_StoryStatus_f, "report story co-op player and world state" );
	SV_CoF_RetailInit();
}
```

- [ ] **Step 5: Run the harness to see it pass**

Run: `& python -m unittest tests.test_enhanced_retail tests.test_enhanced_descriptor -v`
Expected: both `ok`.

- [ ] **Step 6: Build the engine**

Run: `& cmd.exe /d /c tools\build_enhanced.cmd 2>&1 | Select-String "warning|error C|finished"`
Expected: `'build' finished successfully`, and no `warning`/`error C` lines mentioning `sv_cof_retail.c`, `sv_game.c` or `sv_cof_story.c`.

- [ ] **Step 7: Commit both repos**

```bash
git -C "$ENGINE" add engine/server/sv_cof_retail.h engine/server/sv_cof_retail.c engine/server/sv_game.c engine/server/server.h engine/server/sv_cof_story.c
git -C "$ENGINE" commit -m "Add the story co-op retail layer and cof_story_probe" -m "Slots, free slots, give via the game's pickup, removal via inventorydeletenamed,
item names, ProFont and ScreenFade through SV_CoF_SendUserMessage.

Co-Authored-By: Claude Opus 5.5 (1M context) <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_018mJcNynXJSSzUadjrVgU5W"
git add tests/engine/cof_retail_harness.c tests/test_enhanced_retail.py
git commit -m "Test the retail layer against engine doubles" -m "Co-Authored-By: Claude Opus 5.5 (1M context) <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_018mJcNynXJSSzUadjrVgU5W"
```

---

### Task 3: Deploy stage 1 and check the retail paths live (stage gate)

**Files:**
- Modify: `state/enhanced-remote-session.json` (engine hash only; untracked, private)
- Modify: `AGENT-HANDOFF-2026-09-27.md` (new dated UPDATE section)
- Create: `evidence/stage1-retail/` (untracked: build, pre-deploy engine backups, probe logs)

**Interfaces:**
- Consumes (from Task 2): the `cof_story_probe` command.
- Produces: a recorded yes or no for each of these, which the stage 3–4 plan depends on:
  1. The engine's give shows "You got the …" and the inventory icon.
  2. `inventorydeletenamed` issued by the engine removes the icon.
  3. ProFont shows in the game's strip.
  4. ScreenFade blinks.

  The spec's other two live facts, the engine calling `AddDocument` and a code `AddNote`, need stage 4's calls. They move to the stage 3–4 plan's first task.

- [ ] **Step 1: Run the whole suite**

Run: `& python -m unittest discover -s tests 2>&1 | Select-Object -Last 3`
Expected: `OK`. The count is 111 plus the new tests.

- [ ] **Step 2: Stage and deploy the engine to both copies**

Run in PowerShell from the project root:
```powershell
if (Get-CimInstance Win32_Process -Filter "Name='CoFLaunchApp.exe' OR Name='cof.exe'") { throw 'Game is running; not deploying.' }
$build = 'upstream\cof-enhanced\xash3d-fwgs-4857b389e6ba32ddaa68582aedcbc950c138f46a\build-local-20260927\engine'
$stage = 'evidence\stage1-retail'
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

- [ ] **Step 3: Tell the user, then start the host**

Say to the user: "Launching the host now for the stage 1 check (1280×720, `c_start`)." Then run:
`Start-Process powershell.exe -ArgumentList '-NoProfile','-ExecutionPolicy','Bypass','-File','tools\Start-StoryHost.ps1'`
Wait until the host log shows `cofstory phase=gate-enabled`. Check with:
`Select-String -Path runtimes\enhanced\story-remote-host.log -Pattern 'gate-enabled' | Select-Object -Last 1`

- [ ] **Step 4: Probe the retail paths over RCON and record them**

Run each command and save its output under `evidence\stage1-retail\`:
```powershell
$c = 'python'
& $c tools\enhanced_local_control.py "status" --session remote
& $c tools\enhanced_local_control.py "cof_story_probe slots 1" --session remote --out evidence\stage1-retail\01-slots.log
& $c tools\enhanced_local_control.py "sv_cheats 1" --session remote
& $c tools\enhanced_local_control.py "cof_story_probe give 1 inventoryitems/hallkey.txt" --session remote --out evidence\stage1-retail\02-give.log
& $c tools\enhanced_local_control.py "cof_story_probe slots 1" --session remote --out evidence\stage1-retail\03-slots-after-give.log
```
Use the host's userid from `status` if it isn't 1.
Expected:
- `02-give.log` contains `what=give userid=1 result=1`.
- `03-slots-after-give.log` shows `descriptor="inventoryitems/hallkey.txt"` in one slot.

**USER CHECK:** ask the user whether they saw "You got the Hall key" and whether the Hall key is in the inventory screen. Record the answer.

- [ ] **Step 5: Probe removal, message and fade**

```powershell
& $c tools\enhanced_local_control.py "cof_story_probe remove 1 inventoryitems/hallkey.txt" --session remote --out evidence\stage1-retail\04-remove.log
& $c tools\enhanced_local_control.py "cof_story_probe slots 1" --session remote --out evidence\stage1-retail\05-slots-after-remove.log
& $c tools\enhanced_local_control.py "cof_story_probe msg 1 2 \"Please wait until everyone is ready!\"" --session remote --out evidence\stage1-retail\06-msg.log
& $c tools\enhanced_local_control.py "cof_story_probe fade 1" --session remote --out evidence\stage1-retail\07-fade.log
& $c tools\enhanced_local_control.py "sv_cheats 0" --session remote
```
Expected:
- `05-slots-after-remove.log` no longer contains `hallkey`.
- `06-msg.log` and `07-fade.log` report `result=1`.

**USER CHECK:** ask whether the Hall key left the inventory screen, whether the red line "Please wait until everyone is ready!" appeared in the game's message strip, and whether the screen blinked black. Record each answer.

- [ ] **Step 6: Record the results in the handoff and decide the gate**

Add a dated `## UPDATE` section at the top of `AGENT-HANDOFF-2026-09-27.md` containing:
- the engine hash;
- the four yes/no results, with the evidence file names;
- if any answer is no, the observed behavior and "stage 2 blocked on: …".

**If the give or the removal failed, stop here** and report to the user; stage 2 must not be built on a broken path. If only ProFont or ScreenFade failed, continue with stage 2 (it uses ProFont only for announcements) and note it for the stage 3–4 plan.

- [ ] **Step 7: Commit the handoff**

```bash
git add AGENT-HANDOFF-2026-09-27.md
git commit -m "Record stage 1 live results for the retail layer" -m "Co-Authored-By: Claude Opus 5.5 (1M context) <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_018mJcNynXJSSzUadjrVgU5W"
```

---

### Task 4: The item ledger's rules (pure state)

**Files:**
- Create: `$ENGINE/engine/server/sv_cof_share_state.h`
- Create: `tests/engine/cof_share_state_test.c`
- Create: `tests/test_enhanced_share_state.py`

**Interfaces:**
- Consumes (from Task 1): `CoF_IsStoryItem`.
- Produces:
  - `cof_share_state_t`, plus the enums `COF_SHARE_NONE/GIVE/TAKE/ROOM`, `COF_SHARE_CMD_OTHER/DROP` and `COF_SHARE_FOUND/USED/DROPPED/DELIVERED/REMOVED`.
  - `void CoF_ShareReset( cof_share_state_t *s )`.
  - `void CoF_ShareObserve( cof_share_state_t *s, int slot, int userid, int alive, char slots[COF_SHARE_SLOTS][COF_SHARE_NAME] )` (`slots` may be NULL when `userid` is 0).
  - `int CoF_ShareCommandKind( const char *command )`.
  - `void CoF_ShareCommand( cof_share_state_t *s, int slot, int kind, char slots[COF_SHARE_SLOTS][COF_SHARE_NAME] )`.
  - `int CoF_SharePlan( cof_share_state_t *s, int slot, int free_slots, double now, char out[COF_SHARE_NAME] )`.
  - `void CoF_ShareIssued( cof_share_state_t *s, int slot, int action, const char *name, double now )`.
  - `int CoF_ShareFinder( const cof_share_state_t *s, const char *name )`.
  - Constants `COF_SHARE_PLAYERS` (32), `COF_SHARE_SLOTS` (6), `COF_SHARE_NAME` (64).

- [ ] **Step 1: Write the failing rules test**

`tests/engine/cof_share_state_test.c`:
```c
/* The story item ledger's rules, driven directly. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "sv_cof_share_state.h"

#define KEY "inventoryitems/hallkey.txt"
#define LEFT "inventoryitems/leftplate.txt"
#define RIGHT "inventoryitems/rightplate.txt"
#define PLATE "inventoryitems/plate.txt"
#define PHONE "inventoryitems/weapons/weapon_mobile.txt"

static cof_share_state_t s;
static char inv[2][COF_SHARE_SLOTS][COF_SHARE_NAME];
static int alive[2];

static void put( int p, int slot, const char *d ) { strcpy( inv[p][slot], d ); }
static void clear( int p, const char *d )
{
	int i;
	for( i = 0; i < COF_SHARE_SLOTS; i++ )
		if( !strcmp( inv[p][i], d )) inv[p][i][0] = 0;
}
static void look( void )
{
	CoF_ShareObserve( &s, 0, 101, alive[0], inv[0] );
	CoF_ShareObserve( &s, 1, 102, alive[1], inv[1] );
}
static int has_event( int kind, int slot, const char *d )
{
	int i;
	for( i = 0; i < s.event_count; i++ )
		if( s.events[i].kind == kind && s.events[i].slot == slot && !strcmp( s.events[i].name, d )) return 1;
	return 0;
}
static int plan( int p, int free_slots, double now, char *out ) { return CoF_SharePlan( &s, p, free_slots, now, out ); }
static void start( void )
{
	CoF_ShareReset( &s );
	memset( inv, 0, sizeof( inv ));
	alive[0] = alive[1] = 1;
	look();
	s.event_count = 0;
}

int main( void )
{
	char item[COF_SHARE_NAME];

	/* A pickup joins the ledger, remembers its finder, and is planned for the partner. */
	start(); put( 0, 0, KEY ); look();
	assert( has_event( COF_SHARE_FOUND, 0, KEY ) && s.party.count == 1 && CoF_ShareFinder( &s, KEY ) == 101 );
	assert( plan( 1, 6, 10, item ) == COF_SHARE_GIVE && !strcmp( item, KEY ));
	assert( plan( 0, 5, 10, item ) == COF_SHARE_NONE );

	/* The delivery lands: delivered, not found again; nothing more to do. */
	CoF_ShareIssued( &s, 1, COF_SHARE_GIVE, KEY, 10 );
	assert( plan( 1, 6, 10.5, item ) == COF_SHARE_NONE ); /* waiting for the inventory */
	s.event_count = 0; put( 1, 0, KEY ); look();
	assert( has_event( COF_SHARE_DELIVERED, 1, KEY ) && !has_event( COF_SHARE_FOUND, 1, KEY ));
	assert( plan( 1, 5, 11, item ) == COF_SHARE_NONE );

	/* Used up by the finder's game command: the partner's copy is taken. */
	CoF_ShareObserve( &s, 0, 101, 1, inv[0] ); s.event_count = 0;
	clear( 0, KEY ); CoF_ShareCommand( &s, 0, CoF_ShareCommandKind( "inventoryuse" ), inv[0] );
	assert( has_event( COF_SHARE_USED, 0, KEY ) && s.party.count == 0 );
	assert( plan( 1, 5, 12, item ) == COF_SHARE_TAKE && !strcmp( item, KEY ));
	CoF_ShareIssued( &s, 1, COF_SHARE_TAKE, KEY, 12 );
	clear( 1, KEY ); s.event_count = 0; look();
	assert( has_event( COF_SHARE_REMOVED, 1, KEY ) && !has_event( COF_SHARE_USED, 1, KEY ));

	/* Used outside any command (a map script such as cof_clearitems): same result. */
	start(); put( 0, 0, KEY ); put( 1, 0, KEY ); look(); s.event_count = 0;
	clear( 1, KEY ); look();
	assert( has_event( COF_SHARE_USED, 1, KEY ) && s.party.count == 0 );
	assert( plan( 0, 5, 1, item ) == COF_SHARE_TAKE );

	/* A drop sets the item down: the party keeps it, the dropper is not re-given it. */
	start(); put( 0, 0, KEY ); put( 1, 0, KEY ); look(); s.event_count = 0;
	clear( 0, KEY ); CoF_ShareCommand( &s, 0, CoF_ShareCommandKind( "inventorydrop" ), inv[0] );
	assert( has_event( COF_SHARE_DROPPED, 0, KEY ) && s.party.count == 1 );
	assert( plan( 0, 6, 1, item ) == COF_SHARE_NONE && plan( 1, 5, 1, item ) == COF_SHARE_NONE );
	/* ...picked back up later: no new find, and it counts as theirs again. */
	s.event_count = 0; put( 0, 2, KEY ); look();
	assert( !has_event( COF_SHARE_FOUND, 0, KEY ) && s.players[0].set_down.count == 0 );

	/* A full inventory: told once to make room; delivered once a slot frees. */
	start(); put( 0, 0, KEY ); look();
	assert( plan( 1, 0, 1, item ) == COF_SHARE_ROOM && !strcmp( item, KEY ));
	assert( plan( 1, 0, 2, item ) == COF_SHARE_NONE ); /* told only once */
	assert( plan( 1, -1, 3, item ) == COF_SHARE_NONE ); /* unknown room: no promise */
	assert( plan( 1, 1, 4, item ) == COF_SHARE_GIVE );

	/* An unconfirmed give backs off, then retries. */
	start(); put( 0, 0, KEY ); look();
	assert( plan( 1, 6, 1, item ) == COF_SHARE_GIVE ); CoF_ShareIssued( &s, 1, COF_SHARE_GIVE, KEY, 1 );
	assert( plan( 1, 6, 2.5, item ) == COF_SHARE_NONE ); /* expired: backing off */
	assert( plan( 1, 6, 7.0, item ) == COF_SHARE_NONE );
	assert( plan( 1, 6, 8.0, item ) == COF_SHARE_GIVE );

	/* Review focus 1: a delivery that lands after its item was used up is taken again, never re-added. */
	start(); put( 0, 0, KEY ); look();
	assert( plan( 1, 6, 1, item ) == COF_SHARE_GIVE ); CoF_ShareIssued( &s, 1, COF_SHARE_GIVE, KEY, 1 );
	clear( 0, KEY ); CoF_ShareCommand( &s, 0, COF_SHARE_CMD_OTHER, inv[0] );
	s.event_count = 0; put( 1, 0, KEY ); look();
	assert( has_event( COF_SHARE_DELIVERED, 1, KEY ) && s.party.count == 0 );
	assert( plan( 1, 5, 1.2, item ) == COF_SHARE_TAKE );

	/* Review focus 4: death keeps items; the respawn wipe is not a use; everything comes back. */
	start(); put( 0, 0, KEY ); put( 1, 0, KEY ); look();
	alive[1] = 0; look();
	assert( plan( 1, 5, 1, item ) == COF_SHARE_NONE ); /* nothing is handed to the dead */
	clear( 1, KEY ); alive[1] = 1; s.event_count = 0; look(); /* CBasePlayer::Spawn wiped it */
	assert( !has_event( COF_SHARE_USED, 1, KEY ) && s.party.count == 1 );
	assert( plan( 1, 6, 2, item ) == COF_SHARE_GIVE && !strcmp( item, KEY ));
	/* ...and a respawn forgets what was set down. */
	start(); put( 0, 0, KEY ); put( 1, 0, KEY ); look();
	clear( 1, KEY ); CoF_ShareCommand( &s, 1, COF_SHARE_CMD_DROP, inv[1] );
	alive[1] = 0; look(); alive[1] = 1; look();
	assert( plan( 1, 6, 2, item ) == COF_SHARE_GIVE );

	/* Combine: both halves used up and the result found; the partner follows. */
	start(); put( 0, 0, LEFT ); put( 0, 1, RIGHT ); put( 1, 0, LEFT ); put( 1, 1, RIGHT ); look();
	CoF_ShareObserve( &s, 0, 101, 1, inv[0] ); s.event_count = 0;
	clear( 0, LEFT ); clear( 0, RIGHT ); put( 0, 0, PLATE );
	CoF_ShareCommand( &s, 0, CoF_ShareCommandKind( "inventorycombine" ), inv[0] );
	assert( has_event( COF_SHARE_USED, 0, LEFT ) && has_event( COF_SHARE_USED, 0, RIGHT ) && has_event( COF_SHARE_FOUND, 0, PLATE ));
	assert( plan( 1, 4, 1, item ) == COF_SHARE_TAKE );

	/* Weapons, ammo and costumes are never shared. */
	start(); put( 0, 0, PHONE ); look();
	assert( s.party.count == 0 && plan( 1, 6, 1, item ) == COF_SHARE_NONE );

	/* An emptied client slot forgets its player; a newcomer is served. */
	start(); put( 0, 0, KEY ); look();
	CoF_ShareObserve( &s, 1, 0, 0, NULL );
	assert( s.players[1].userid == 0 );
	CoF_ShareObserve( &s, 1, 205, 1, inv[1] );
	assert( plan( 1, 6, 1, item ) == COF_SHARE_GIVE );

	/* A new session starts with an empty ledger. */
	CoF_ShareReset( &s );
	assert( s.party.count == 0 && !s.players[0].userid );
	puts( "share state scenarios passed" );
	return 0;
}
```

`tests/test_enhanced_share_state.py`:
```python
"""The story item ledger's rules, run natively offline."""
import unittest

from tests.native import ENGINE_SERVER, run_native


class ShareStateTests(unittest.TestCase):
    def test_ledger_rules(self):
        out = run_native(self, 'cof_share_state_test.c', {
            'sv_cof_descriptor.h': (ENGINE_SERVER / 'sv_cof_descriptor.h').read_text(),
            'sv_cof_share_state.h': (ENGINE_SERVER / 'sv_cof_share_state.h').read_text(),
        })
        self.assertIn('share state scenarios passed', out)


if __name__ == '__main__':
    unittest.main()
```

- [ ] **Step 2: Run it to see it fail**

Run: `& python -m unittest tests.test_enhanced_share_state -v`
Expected: ERROR with `FileNotFoundError` for `sv_cof_share_state.h`.

- [ ] **Step 3: Write the ledger**

`$ENGINE/engine/server/sv_cof_share_state.h`:
```c
/* Story item ledger for Cry of Fear story co-op. Pure state, no engine types,
 * so the offline harness drives it directly. GPL-3.0-or-later.
 * Rules: docs/superpowers/specs/2026-09-30-coop-split-play-design.md, part 2
 * (project repo). A player can never hold two copies of one descriptor
 * (retail AddInventoryItem 100DAD40 has an exact duplicate check), so the
 * party ledger is a set. Desired state: every player holds every party item
 * except those they set down, and nothing the party no longer has. */
#ifndef SV_COF_SHARE_STATE_H
#define SV_COF_SHARE_STATE_H
#include <string.h>
#include "sv_cof_descriptor.h"

#define COF_SHARE_PLAYERS 32
#define COF_SHARE_SLOTS 6
#define COF_SHARE_ITEMS 64
#define COF_SHARE_NAME 64
#define COF_SHARE_EVENTS 16
#define COF_SHARE_CONFIRM 1.0 // an issued give or take shows in the inventory within this
#define COF_SHARE_RETRY 5.0 // then it waits this long before trying again

enum { COF_SHARE_NONE, COF_SHARE_GIVE, COF_SHARE_TAKE, COF_SHARE_ROOM };
enum { COF_SHARE_CMD_OTHER, COF_SHARE_CMD_DROP };
enum { COF_SHARE_FOUND, COF_SHARE_USED, COF_SHARE_DROPPED, COF_SHARE_DELIVERED, COF_SHARE_REMOVED };

typedef struct { char name[COF_SHARE_NAME]; int finder; } cof_share_item_t;
typedef struct { int count; cof_share_item_t items[COF_SHARE_ITEMS]; } cof_share_set_t;

typedef struct
{
	int userid; // 0: nobody in this client slot
	int alive;
	char held[COF_SHARE_SLOTS][COF_SHARE_NAME]; // story items at the last look, "" otherwise
	cof_share_set_t set_down; // dropped on purpose: not re-delivered
	cof_share_set_t room_told; // already told to make room for these
	int pending; // COF_SHARE_GIVE or COF_SHARE_TAKE awaiting the inventory
	char pending_name[COF_SHARE_NAME];
	double pending_until, retry_after;
} cof_share_player_t;

typedef struct { int kind, slot, userid; char name[COF_SHARE_NAME]; } cof_share_event_t;

typedef struct
{
	cof_share_set_t party;
	cof_share_player_t players[COF_SHARE_PLAYERS];
	int event_count; // drained by the engine glue every frame
	cof_share_event_t events[COF_SHARE_EVENTS];
} cof_share_state_t;

static void CoF_ShareCopy( char *dst, const char *src )
{
	size_t i;
	for( i = 0; i + 1 < COF_SHARE_NAME && src[i]; i++ ) dst[i] = src[i];
	dst[i] = 0;
}

static int CoF_ShareSetFind( const cof_share_set_t *set, const char *name )
{
	int i;
	for( i = 0; i < set->count; i++ )
		if( !strcmp( set->items[i].name, name )) return i;
	return -1;
}

static int CoF_ShareSetAdd( cof_share_set_t *set, const char *name, int finder )
{
	if( !name[0] || strlen( name ) >= COF_SHARE_NAME || set->count >= COF_SHARE_ITEMS || CoF_ShareSetFind( set, name ) >= 0 ) return 0;
	CoF_ShareCopy( set->items[set->count].name, name );
	set->items[set->count].finder = finder;
	set->count++;
	return 1;
}

static int CoF_ShareSetRemove( cof_share_set_t *set, const char *name )
{
	int i = CoF_ShareSetFind( set, name );
	if( i < 0 ) return 0;
	set->items[i] = set->items[--set->count];
	return 1;
}

static int CoF_ShareHolds( const cof_share_player_t *p, const char *name )
{
	int i;
	for( i = 0; i < COF_SHARE_SLOTS; i++ )
		if( !strcmp( p->held[i], name )) return 1;
	return 0;
}

static void CoF_ShareReset( cof_share_state_t *s )
{
	memset( s, 0, sizeof( *s ));
}

static void CoF_ShareEvent( cof_share_state_t *s, int kind, int slot, const char *name )
{
	cof_share_event_t *e;
	if( s->event_count >= COF_SHARE_EVENTS ) return; // only log lines are lost
	e = &s->events[s->event_count++];
	e->kind = kind;
	e->slot = slot;
	e->userid = s->players[slot].userid;
	CoF_ShareCopy( e->name, name );
}

static void CoF_ShareStoryOnly( char out[COF_SHARE_SLOTS][COF_SHARE_NAME], char slots[COF_SHARE_SLOTS][COF_SHARE_NAME] )
{
	int i;
	for( i = 0; i < COF_SHARE_SLOTS; i++ )
	{
		out[i][0] = 0;
		if( slots && CoF_IsStoryItem( slots[i] ) && strlen( slots[i] ) < COF_SHARE_NAME ) CoF_ShareCopy( out[i], slots[i] );
	}
}

static void CoF_ShareFound( cof_share_state_t *s, int slot, const char *name )
{
	cof_share_player_t *p = &s->players[slot];
	CoF_ShareSetRemove( &p->set_down, name );
	if( CoF_ShareSetAdd( &s->party, name, p->userid )) CoF_ShareEvent( s, COF_SHARE_FOUND, slot, name );
}

static void CoF_ShareUsed( cof_share_state_t *s, int slot, const char *name )
{
	if( CoF_ShareSetRemove( &s->party, name )) CoF_ShareEvent( s, COF_SHARE_USED, slot, name );
}

// One player's inventory changed from p->held to held. ignore_losses: the
// player is dead or was just respawned (the Spawn wipe is not a use).
// dropping: the change came from an inventorydrop command.
static void CoF_ShareApply( cof_share_state_t *s, int slot, char held[COF_SHARE_SLOTS][COF_SHARE_NAME], int ignore_losses, int dropping )
{
	cof_share_player_t *p = &s->players[slot];
	int i, j, kept;
	for( i = 0; i < COF_SHARE_SLOTS; i++ ) // losses
	{
		const char *name = p->held[i];
		if( !name[0] ) continue;
		for( kept = j = 0; j < COF_SHARE_SLOTS && !kept; j++ ) kept = !strcmp( held[j], name );
		if( kept ) continue;
		if( p->pending == COF_SHARE_TAKE && !strcmp( p->pending_name, name ))
		{
			p->pending = COF_SHARE_NONE;
			CoF_ShareEvent( s, COF_SHARE_REMOVED, slot, name );
		}
		else if( ignore_losses ) continue;
		else if( dropping )
		{
			CoF_ShareSetAdd( &p->set_down, name, 0 );
			CoF_ShareEvent( s, COF_SHARE_DROPPED, slot, name );
		}
		else CoF_ShareUsed( s, slot, name );
	}
	for( i = 0; i < COF_SHARE_SLOTS; i++ ) // gains
	{
		const char *name = held[i];
		if( !name[0] || CoF_ShareHolds( p, name )) continue;
		if( p->pending == COF_SHARE_GIVE && !strcmp( p->pending_name, name ))
		{
			p->pending = COF_SHARE_NONE;
			CoF_ShareEvent( s, COF_SHARE_DELIVERED, slot, name );
			continue; // a late delivery must not bring back an item used up meanwhile
		}
		CoF_ShareFound( s, slot, name );
	}
	memcpy( p->held, held, sizeof( p->held ));
}

// A frame's look at one client slot. userid 0: nobody there (slots may be NULL).
static void CoF_ShareObserve( cof_share_state_t *s, int slot, int userid, int alive, char slots[COF_SHARE_SLOTS][COF_SHARE_NAME] )
{
	cof_share_player_t *p;
	char held[COF_SHARE_SLOTS][COF_SHARE_NAME];
	int respawned;
	if( slot < 0 || slot >= COF_SHARE_PLAYERS ) return;
	p = &s->players[slot];
	if( !userid )
	{
		memset( p, 0, sizeof( *p ));
		return;
	}
	CoF_ShareStoryOnly( held, slots );
	if( p->userid != userid )
	{
		// A new player in this slot: whatever they carry becomes the party's.
		memset( p, 0, sizeof( *p ));
		p->userid = userid;
		p->alive = alive;
		CoF_ShareApply( s, slot, held, 0, 0 );
		return;
	}
	respawned = alive && !p->alive; // CBasePlayer::Spawn wiped the inventory
	CoF_ShareApply( s, slot, held, respawned || !alive, 0 );
	if( respawned )
	{
		memset( &p->set_down, 0, sizeof( p->set_down ));
		memset( &p->room_told, 0, sizeof( p->room_told ));
	}
	p->alive = alive;
}

static int CoF_ShareCommandKind( const char *command )
{
	return !strcmp( command, "inventorydrop" ) ? COF_SHARE_CMD_DROP : COF_SHARE_CMD_OTHER;
}

// After a game client command from this player. The glue refreshed the look
// with CoF_ShareObserve right before the command ran, so this change is
// exactly the command's doing.
static void CoF_ShareCommand( cof_share_state_t *s, int slot, int kind, char slots[COF_SHARE_SLOTS][COF_SHARE_NAME] )
{
	char held[COF_SHARE_SLOTS][COF_SHARE_NAME];
	if( slot < 0 || slot >= COF_SHARE_PLAYERS || !s->players[slot].userid ) return;
	CoF_ShareStoryOnly( held, slots );
	CoF_ShareApply( s, slot, held, !s->players[slot].alive, kind == COF_SHARE_CMD_DROP );
}

// The next step toward the desired state for one player. free_slots: usable
// empty inventory slots, -1 when unknown. The item goes to out.
static int CoF_SharePlan( cof_share_state_t *s, int slot, int free_slots, double now, char out[COF_SHARE_NAME] )
{
	cof_share_player_t *p;
	int i;
	out[0] = 0;
	if( slot < 0 || slot >= COF_SHARE_PLAYERS ) return COF_SHARE_NONE;
	p = &s->players[slot];
	if( !p->userid || !p->alive ) return COF_SHARE_NONE;
	if( p->pending )
	{
		if( now < p->pending_until ) return COF_SHARE_NONE;
		p->pending = COF_SHARE_NONE; // never confirmed: back off, then retry
		p->retry_after = now + COF_SHARE_RETRY;
	}
	if( now < p->retry_after ) return COF_SHARE_NONE;
	for( i = 0; i < COF_SHARE_SLOTS; i++ ) // copies of items the party used up
		if( p->held[i][0] && CoF_ShareSetFind( &s->party, p->held[i] ) < 0 )
		{
			CoF_ShareCopy( out, p->held[i] );
			return COF_SHARE_TAKE;
		}
	for( i = 0; i < s->party.count; i++ )
	{
		const char *name = s->party.items[i].name;
		if( CoF_ShareHolds( p, name ) || CoF_ShareSetFind( &p->set_down, name ) >= 0 ) continue;
		if( free_slots > 0 )
		{
			CoF_ShareCopy( out, name );
			return COF_SHARE_GIVE;
		}
		if( free_slots < 0 ) continue;
		if( CoF_ShareSetAdd( &p->room_told, name, 0 ))
		{
			CoF_ShareCopy( out, name );
			return COF_SHARE_ROOM;
		}
	}
	return COF_SHARE_NONE;
}

static void CoF_ShareIssued( cof_share_state_t *s, int slot, int action, const char *name, double now )
{
	cof_share_player_t *p;
	if( slot < 0 || slot >= COF_SHARE_PLAYERS || ( action != COF_SHARE_GIVE && action != COF_SHARE_TAKE )) return;
	p = &s->players[slot];
	p->pending = action;
	CoF_ShareCopy( p->pending_name, name );
	p->pending_until = now + COF_SHARE_CONFIRM;
}

static int CoF_ShareFinder( const cof_share_state_t *s, const char *name )
{
	int i = CoF_ShareSetFind( &s->party, name );
	return i < 0 ? 0 : s->party.items[i].finder;
}
#endif
```

- [ ] **Step 4: Run it to see it pass**

Run: `& python -m unittest tests.test_enhanced_share_state -v`
Expected: `test_ledger_rules ... ok`

- [ ] **Step 5: Commit both repos**

```bash
git -C "$ENGINE" add engine/server/sv_cof_share_state.h
git -C "$ENGINE" commit -m "Add the story item ledger's rules as pure state" -m "Co-Authored-By: Claude Opus 5.5 (1M context) <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_018mJcNynXJSSzUadjrVgU5W"
git add tests/engine/cof_share_state_test.c tests/test_enhanced_share_state.py
git commit -m "Test the story item ledger's rules" -m "Co-Authored-By: Claude Opus 5.5 (1M context) <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_018mJcNynXJSSzUadjrVgU5W"
```

---

### Task 5: The ledger in the engine

**Files:**
- Create: `$ENGINE/engine/server/sv_cof_share.h`
- Create: `$ENGINE/engine/server/sv_cof_share.c`
- Modify: `$ENGINE/engine/server/sv_client.c` (the `pfnClientCommand` call in `SV_ExecuteClientCommand`, and the includes)
- Modify: `$ENGINE/engine/server/sv_cof_story.c` (`SV_CoF_StoryInit`, `SV_CoF_StoryFrame`, `SV_CoF_StoryShutdown`, and the includes)
- Modify: `$ENGINE/engine/server/sv_save.c` (`SV_CoF_TransitionReset` and the includes)
- Create: `tests/engine/cof_share_harness.c`
- Create: `tests/test_enhanced_share.py`

**Interfaces:**
- Consumes:
  - Task 2: `SV_CoF_RetailReadSlots`, `SV_CoF_RetailFreeSlots`, `SV_CoF_RetailGiveItem`, `SV_CoF_RetailRemoveItem`, `SV_CoF_RetailItemName`, `SV_CoF_RetailProFont`, `COF_RETAIL_SLOTS`, `COF_RETAIL_ITEM`.
  - Task 4: all of `sv_cof_share_state.h`.
- Produces:
  - `void SV_CoF_ShareInit( void )`, which registers `cof_story_ledger`.
  - `void SV_CoF_ShareReset( void )`.
  - `void SV_CoF_ShareFrame( void )`.
  - `void SV_CoF_ShareCommandBegin( sv_client_t *cl )`.
  - `void SV_CoF_ShareCommandEnd( sv_client_t *cl )`.

- [ ] **Step 1: Write the failing glue harness**

`tests/engine/cof_share_harness.c`:
```c
/* Offline doubles for sv_cof_share.c: game inventories as plain arrays. */
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
typedef int qboolean;
#define true 1
#define false 0
#define cs_free 0
#define cs_connected 2
#define cs_spawned 4
#define FCL_FAKECLIENT ( 1 << 7 )
#define DEAD_NO 0
#define FBitSet( a, b ) (( a ) & ( b ))
#define COF_RETAIL_SLOTS 6
#define COF_RETAIL_ITEM 64
#define Q_snprintf snprintf
typedef struct { struct { float health; int deadflag; } v; } edict_t;
typedef struct { int state, userid; unsigned int flags; char name[32]; edict_t *edict; } sv_client_t;
typedef void ( *xcommand_t )( void );
static struct { int maxclients; sv_client_t *clients; } svs;
static struct { double time; } sv;
static sv_client_t clients[2];
static edict_t edicts[2];
static char inv[2][COF_RETAIL_SLOTS][COF_RETAIL_ITEM], last_line[2][192], console[32768], command_name[32];
static int three[2], gives, takes, refuse_give;
static size_t console_length;
static xcommand_t ledger;

static int who( sv_client_t *cl ) { return (int)( cl - clients ); }
static qboolean SV_CoF_StoryCoopEnabled( void ) { return true; }
static const char *Cmd_Argv( int i ) { return i == 0 ? command_name : ""; }
static void Cmd_AddCommand( const char *name, xcommand_t f, const char *desc ) { ledger = f; }
static void Con_Printf( const char *fmt, ... )
{
	va_list args;
	va_start( args, fmt );
	console_length += (size_t)vsnprintf( console + console_length, sizeof( console ) - console_length, fmt, args );
	va_end( args );
}
static qboolean SV_CoF_RetailReadSlots( sv_client_t *cl, char slots[COF_RETAIL_SLOTS][COF_RETAIL_ITEM] )
{
	if( cl->state != cs_spawned ) return false;
	memcpy( slots, inv[who( cl )], sizeof( inv[0] ));
	return true;
}
static int SV_CoF_RetailFreeSlots( sv_client_t *cl )
{
	int i, n = 0, usable = three[who( cl )] ? 3 : 6;
	for( i = 0; i < usable; i++ ) if( !inv[who( cl )][i][0] ) n++;
	return n;
}
/* the game's AddInventoryItem: first free usable slot, never a second copy */
static qboolean SV_CoF_RetailGiveItem( sv_client_t *cl, const char *d )
{
	int i, p = who( cl );
	gives++;
	if( refuse_give ) return false;
	for( i = 0; i < 6; i++ ) if( !strcmp( inv[p][i], d )) return true;
	for( i = 0; i < ( three[p] ? 3 : 6 ); i++ ) if( !inv[p][i][0] ) { strcpy( inv[p][i], d ); return true; }
	return false;
}
static qboolean SV_CoF_RetailRemoveItem( sv_client_t *cl, const char *d )
{
	int i, p = who( cl );
	takes++;
	for( i = 0; i < 6; i++ ) if( !strcmp( inv[p][i], d )) { inv[p][i][0] = 0; break; }
	return true;
}
static qboolean SV_CoF_RetailItemName( const char *d, char *out, size_t size )
{
	snprintf( out, size, "%s", strstr( d, "hallkey" ) ? "Hall key" : "fuse" );
	return true;
}
static qboolean SV_CoF_RetailProFont( sv_client_t *cl, int colour, const char *text, qboolean reliable )
{
	snprintf( last_line[who( cl )], sizeof( last_line[0] ), "%d:%s", colour, text );
	return true;
}

#include "sv_cof_descriptor.h"
#include "sv_cof_share_state.h"
#include "share_under_test.c"

#define KEY "inventoryitems/hallkey.txt"
#define GLOCK "inventoryitems/weapons/weapon_glock.txt"

static void setup( void )
{
	int i;
	memset( clients, 0, sizeof( clients ));
	memset( inv, 0, sizeof( inv ));
	memset( three, 0, sizeof( three ));
	memset( last_line, 0, sizeof( last_line ));
	gives = takes = refuse_give = 0;
	console[0] = 0; console_length = 0;
	sv.time = 10;
	svs.maxclients = 2;
	svs.clients = clients;
	for( i = 0; i < 2; i++ )
	{
		clients[i].state = cs_spawned;
		clients[i].userid = i + 1;
		clients[i].edict = &edicts[i];
		snprintf( clients[i].name, sizeof( clients[i].name ), "Little Rascal %d", i + 1 );
		edicts[i].v.health = 100;
		edicts[i].v.deadflag = DEAD_NO;
	}
	SV_CoF_ShareReset();
	SV_CoF_ShareFrame();
}
static void frame( double dt ) { sv.time += dt; SV_CoF_ShareFrame(); }
static void begin( int p, const char *name ) { strcpy( command_name, name ); SV_CoF_ShareCommandBegin( &clients[p] ); }
static void end( int p ) { SV_CoF_ShareCommandEnd( &clients[p] ); }
static int holds( int p, const char *d ) { int i; for( i = 0; i < 6; i++ ) if( !strcmp( inv[p][i], d )) return 1; return 0; }
static void lose( int p, const char *d ) { int i; for( i = 0; i < 6; i++ ) if( !strcmp( inv[p][i], d )) inv[p][i][0] = 0; }
static void gain( int p, const char *d ) { int i; for( i = 0; i < 6; i++ ) if( !inv[p][i][0] ) { strcpy( inv[p][i], d ); return; } }

int main( void )
{
	int i;

	/* A pickup is handed to the partner through the game, with a line naming the finder. */
	setup(); gain( 0, KEY ); frame( 0.1 );
	assert( holds( 1, KEY ) && gives == 1 );
	assert( !strcmp( last_line[1], "5:Little Rascal 1 found the Hall key" ) && !last_line[0][0] );
	assert( strstr( console, "cofstory phase=item-found userid=1 item=" KEY ));
	frame( 0.1 );
	assert( gives == 1 && strstr( console, "cofstory phase=item-delivered userid=2 item=" KEY ));

	/* Used at a lock (inventoryuse): the partner's copy goes through the game's removal. */
	begin( 0, "inventoryuse" ); lose( 0, KEY ); end( 0 ); frame( 0.1 );
	assert( !holds( 1, KEY ) && takes == 1 && strstr( console, "cofstory phase=item-used userid=1" ));
	frame( 0.1 );
	assert( strstr( console, "cofstory phase=item-removed userid=2" ) && takes == 1 );

	/* Dropped (inventorydrop): the partner keeps it; the dropper is not re-given it. */
	setup(); gain( 0, KEY ); frame( 0.1 ); frame( 0.1 );
	begin( 0, "inventorydrop" ); lose( 0, KEY ); end( 0 ); frame( 0.1 ); frame( 2 );
	assert( holds( 1, KEY ) && !holds( 0, KEY ) && gives == 1 && takes == 0 );
	assert( strstr( console, "cofstory phase=item-dropped userid=1" ));

	/* Review focus 2: a pickup just before any command still counts as found. */
	setup(); gain( 0, KEY ); begin( 0, "say" ); end( 0 ); frame( 0.1 );
	assert( holds( 1, KEY ) && strstr( console, "cofstory phase=item-found userid=1" ));

	/* Full inventory: told once to make room; delivered when a slot frees. */
	setup();
	for( i = 0; i < 6; i++ ) strcpy( inv[1][i], GLOCK );
	gain( 0, KEY ); frame( 0.1 );
	assert( !holds( 1, KEY ) && strstr( last_line[1], "5:Little Rascal 1 found the Hall key. Make room to carry it." ));
	assert( strstr( console, "cofstory phase=item-deferred userid=2 item=" KEY " reason=inventory-full" ));
	last_line[1][0] = 0; frame( 0.1 );
	assert( !last_line[1][0] );
	inv[1][3][0] = 0; frame( 0.1 );
	assert( holds( 1, KEY ));

	/* Three-slot mode counts only slots 1..3. */
	setup(); three[1] = 1;
	for( i = 0; i < 3; i++ ) strcpy( inv[1][i], GLOCK );
	gain( 0, KEY ); frame( 0.1 );
	assert( !holds( 1, KEY ) && gives == 0 );

	/* Review focus 4: the respawn wipe is not a use, and the party's items come back. */
	setup(); gain( 0, KEY ); frame( 0.1 ); frame( 0.1 );
	edicts[1].v.health = 0; frame( 0.1 );
	lose( 1, KEY ); edicts[1].v.health = 100; frame( 0.1 );
	assert( !strstr( console, "phase=item-used" ) && holds( 1, KEY ) && holds( 0, KEY ));

	/* Review focus 3: a partner mid level change (connected, not spawned) is served after sign-on. */
	setup(); clients[1].state = cs_connected; gain( 0, KEY ); frame( 0.1 );
	assert( !holds( 1, KEY ) && gives == 0 );
	clients[1].state = cs_spawned; frame( 0.1 );
	assert( holds( 1, KEY ));

	/* Disconnect, then a new player in the slot: they get the party's items. */
	setup(); gain( 0, KEY ); frame( 0.1 ); frame( 0.1 );
	clients[1].state = cs_free; frame( 0.1 );
	clients[1].state = cs_spawned; clients[1].userid = 7; memset( inv[1], 0, sizeof( inv[1] )); frame( 0.1 );
	assert( holds( 1, KEY ));

	/* A refused give is logged, backs off, and retries. */
	setup(); refuse_give = 1; gain( 0, KEY ); frame( 0.1 );
	assert( gives == 1 && strstr( console, "cofstory phase=item-deferred userid=2 item=" KEY ));
	frame( 1.2 );
	assert( gives == 1 );
	frame( 5.5 );
	assert( gives == 2 );

	/* Fake clients and weapons are never involved. */
	setup(); clients[1].flags = FCL_FAKECLIENT; gain( 0, KEY ); frame( 0.1 );
	assert( gives == 0 );
	setup(); gain( 0, GLOCK ); frame( 0.1 );
	assert( gives == 0 && !strstr( console, "item-found" ));

	/* A new session empties the ledger: nothing is handed out afterwards. */
	setup(); gain( 0, KEY ); frame( 0.1 );
	SV_CoF_ShareReset(); memset( inv, 0, sizeof( inv )); frame( 0.1 ); frame( 0.1 );
	assert( gives == 1 && !holds( 0, KEY ) && !holds( 1, KEY ));

	/* The ledger command lists the party's items. */
	setup(); gain( 0, KEY ); frame( 0.1 ); SV_CoF_ShareInit(); ledger();
	assert( strstr( console, "cofstory phase=ledger-item item=" KEY " finder=1" ));
	puts( "share scenarios passed" );
	return 0;
}
```

`tests/test_enhanced_share.py`:
```python
"""The story item ledger's engine glue against offline doubles."""
import unittest

from tests.native import ENGINE_SERVER, run_native, strip_includes


class ShareTests(unittest.TestCase):
    def test_ledger_in_the_engine(self):
        out = run_native(self, 'cof_share_harness.c', {
            'sv_cof_descriptor.h': (ENGINE_SERVER / 'sv_cof_descriptor.h').read_text(),
            'sv_cof_share_state.h': (ENGINE_SERVER / 'sv_cof_share_state.h').read_text(),
            'share_under_test.c': strip_includes((ENGINE_SERVER / 'sv_cof_share.c').read_text()),
        })
        self.assertIn('share scenarios passed', out)


if __name__ == '__main__':
    unittest.main()
```

- [ ] **Step 2: Run it to see it fail**

Run: `& python -m unittest tests.test_enhanced_share -v`
Expected: ERROR with `FileNotFoundError` for `sv_cof_share.c`.

- [ ] **Step 3: Write the glue**

`$ENGINE/engine/server/sv_cof_share.h`:
```c
/* Opt-in Cry of Fear story co-op: shared story items. GPL-3.0-or-later. */
#ifndef SV_COF_SHARE_H
#define SV_COF_SHARE_H
void SV_CoF_ShareInit( void );
void SV_CoF_ShareReset( void );
void SV_CoF_ShareFrame( void );
// Around the game DLL's ClientCommand, so its inventory changes are attributed exactly.
void SV_CoF_ShareCommandBegin( sv_client_t *cl );
void SV_CoF_ShareCommandEnd( sv_client_t *cl );
#endif
```

`$ENGINE/engine/server/sv_cof_share.c`:
```c
/* Cry of Fear story co-op: shared story items. GPL-3.0-or-later.
 * The engine side of sv_cof_share_state.h. It reads inventories through the
 * retail layer, attributes changes to the game's own client commands, and
 * hands items over or takes used-up copies away through the game's own code. */
#include "common.h"
#include "server.h"
#include "sv_cof_story.h"
#include "sv_cof_retail.h"
#include "sv_cof_share_state.h"
#include "sv_cof_share.h"

#if COF_RETAIL_SLOTS != COF_SHARE_SLOTS || COF_RETAIL_ITEM != COF_SHARE_NAME
#error the retail inventory and the ledger must agree on slots and name size
#endif

static cof_share_state_t cof_share;
static int cof_share_command_slot = -1, cof_share_command_kind;

void SV_CoF_ShareReset( void )
{
	CoF_ShareReset( &cof_share );
	cof_share_command_slot = -1;
}

static int CoF_ShareClientSlot( const sv_client_t *cl )
{
	int slot = (int)( cl - svs.clients );
	return ( slot >= 0 && slot < svs.maxclients && slot < COF_SHARE_PLAYERS ) ? slot : -1;
}

static qboolean CoF_ShareRead( sv_client_t *cl, char slots[COF_SHARE_SLOTS][COF_SHARE_NAME], int *alive )
{
	if( cl->state != cs_spawned || FBitSet( cl->flags, FCL_FAKECLIENT ) || !SV_CoF_RetailReadSlots( cl, slots )) return false;
	*alive = cl->edict->v.health > 0 && cl->edict->v.deadflag == DEAD_NO;
	return true;
}

static const char *CoF_ShareName( int userid )
{
	int i;
	for( i = 0; i < svs.maxclients; i++ )
		if( svs.clients[i].state >= cs_connected && svs.clients[i].userid == userid ) return svs.clients[i].name;
	return "Your partner";
}

static void CoF_ShareDrain( void )
{
	static const char *phases[] = { "item-found", "item-used", "item-dropped", "item-delivered", "item-removed" };
	int i, j;
	for( i = 0; i < cof_share.event_count; i++ )
	{
		const cof_share_event_t *e = &cof_share.events[i];
		char nice[COF_SHARE_NAME], text[160];
		Con_Printf( "cofstory phase=%s userid=%d item=%s\n", phases[e->kind], e->userid, e->name );
		if( e->kind != COF_SHARE_FOUND ) continue;
		SV_CoF_RetailItemName( e->name, nice, sizeof( nice ));
		Q_snprintf( text, sizeof( text ), "%s found the %s", CoF_ShareName( e->userid ), nice );
		for( j = 0; j < svs.maxclients; j++ )
			if( j != e->slot && svs.clients[j].state == cs_spawned ) SV_CoF_RetailProFont( &svs.clients[j], 5, text, true );
	}
	cof_share.event_count = 0;
}

// Before the game's ClientCommand: bring this player's look up to date, so a
// pickup made earlier in the same packet is not credited to the command.
void SV_CoF_ShareCommandBegin( sv_client_t *cl )
{
	char slots[COF_SHARE_SLOTS][COF_SHARE_NAME];
	int alive, slot = CoF_ShareClientSlot( cl );
	cof_share_command_slot = -1;
	if( slot < 0 || !SV_CoF_StoryCoopEnabled() || !CoF_ShareRead( cl, slots, &alive )) return;
	CoF_ShareObserve( &cof_share, slot, cl->userid, alive, slots );
	cof_share_command_kind = CoF_ShareCommandKind( Cmd_Argv( 0 ));
	cof_share_command_slot = slot;
}

void SV_CoF_ShareCommandEnd( sv_client_t *cl )
{
	char slots[COF_SHARE_SLOTS][COF_SHARE_NAME];
	int alive, slot = CoF_ShareClientSlot( cl );
	if( slot < 0 || slot != cof_share_command_slot ) return;
	cof_share_command_slot = -1;
	if( CoF_ShareRead( cl, slots, &alive )) CoF_ShareCommand( &cof_share, slot, cof_share_command_kind, slots );
}

void SV_CoF_ShareFrame( void )
{
	char slots[COF_SHARE_SLOTS][COF_SHARE_NAME], item[COF_SHARE_NAME];
	int i, alive, action;
	for( i = 0; i < svs.maxclients && i < COF_SHARE_PLAYERS; i++ )
	{
		sv_client_t *cl = &svs.clients[i];
		if( cl->state < cs_connected || FBitSet( cl->flags, FCL_FAKECLIENT )) CoF_ShareObserve( &cof_share, i, 0, 0, NULL );
		else if( CoF_ShareRead( cl, slots, &alive )) CoF_ShareObserve( &cof_share, i, cl->userid, alive, slots );
		// connected but not spawned (joining, or mid level change): keep what we know
	}
	CoF_ShareDrain();
	for( i = 0; i < svs.maxclients && i < COF_SHARE_PLAYERS; i++ )
	{
		sv_client_t *cl = &svs.clients[i];
		if( cl->state != cs_spawned ) continue;
		action = CoF_SharePlan( &cof_share, i, SV_CoF_RetailFreeSlots( cl ), sv.time, item );
		if( action == COF_SHARE_GIVE )
		{
			if( !SV_CoF_RetailGiveItem( cl, item )) Con_Printf( "cofstory phase=item-deferred userid=%d item=%s\n", cl->userid, item );
			CoF_ShareIssued( &cof_share, i, action, item, sv.time ); // confirmed only by the inventory
		}
		else if( action == COF_SHARE_TAKE )
		{
			SV_CoF_RetailRemoveItem( cl, item );
			CoF_ShareIssued( &cof_share, i, action, item, sv.time );
		}
		else if( action == COF_SHARE_ROOM )
		{
			char nice[COF_SHARE_NAME], text[160];
			SV_CoF_RetailItemName( item, nice, sizeof( nice ));
			Q_snprintf( text, sizeof( text ), "%s found the %s. Make room to carry it.", CoF_ShareName( CoF_ShareFinder( &cof_share, item )), nice );
			SV_CoF_RetailProFont( cl, 5, text, true );
			Con_Printf( "cofstory phase=item-deferred userid=%d item=%s reason=inventory-full\n", cl->userid, item );
		}
	}
}

static void CoF_ShareLedger_f( void )
{
	int i, j;
	Con_Printf( "cofstory phase=ledger items=%d\n", cof_share.party.count );
	for( i = 0; i < cof_share.party.count; i++ )
		Con_Printf( "cofstory phase=ledger-item item=%s finder=%d\n", cof_share.party.items[i].name, cof_share.party.items[i].finder );
	for( i = 0; i < COF_SHARE_PLAYERS; i++ )
		for( j = 0; j < cof_share.players[i].set_down.count; j++ )
			Con_Printf( "cofstory phase=ledger-set-down userid=%d item=%s\n", cof_share.players[i].userid, cof_share.players[i].set_down.items[j].name );
}

void SV_CoF_ShareInit( void )
{
	Cmd_AddCommand( "cof_story_ledger", CoF_ShareLedger_f, "story co-op: list the party's shared story items" );
}
```

- [ ] **Step 4: Run the glue harness to see it pass**

Run: `& python -m unittest tests.test_enhanced_share tests.test_enhanced_share_state -v`
Expected: both `ok`.

- [ ] **Step 5: Wire the ledger into the engine**

`$ENGINE/engine/server/sv_client.c`: add `#include "sv_cof_share.h"` after `#include "sv_cof_party.h"`, and replace the game DLL call in `SV_ExecuteClientCommand`:
```c
		// custom client commands
		svgame.dllFuncs.pfnClientCommand( cl->edict );
```
with:
```c
		// custom client commands; story co-op attributes their inventory changes
		SV_CoF_ShareCommandBegin( cl );
		svgame.dllFuncs.pfnClientCommand( cl->edict );
		SV_CoF_ShareCommandEnd( cl );
```

`$ENGINE/engine/server/sv_cof_story.c`:
- Add `#include "sv_cof_share.h"` after `#include "sv_cof_retail.h"`.
- In `SV_CoF_StoryInit`, add `SV_CoF_ShareInit();` after `SV_CoF_RetailInit();`.
- Replace `SV_CoF_StoryFrame` and `SV_CoF_StoryShutdown` with:
```c
void SV_CoF_StoryFrame( void )
{
	if( !SV_CoF_StoryCoopEnabled() || sv.state != ss_active ) return;
	SV_CoF_PartyFrame();
	SV_CoF_ShareFrame();
}
```
```c
void SV_CoF_StoryShutdown( void )
{
	SV_CoF_PartyShutdown();
	SV_CoF_ShareReset();
	CoF_StoryCloseGate();
	SV_CoF_ClearValidation();
}
```

`$ENGINE/engine/server/sv_save.c`:
- Add `#include "sv_cof_share.h"` after `#include "sv_cof_party.h"`.
- In `SV_CoF_TransitionReset`, replace `if( clear_history ) cof_story_map_count = 0;` with:
```c
	if( clear_history )
	{
		// A new session (map, save load, non-story changelevel, shutdown):
		// nobody should be handed the previous party's items.
		cof_story_map_count = 0;
		SV_CoF_ShareReset();
	}
```

- [ ] **Step 6: Build and run the whole suite**

Run:
```powershell
& cmd.exe /d /c tools\build_enhanced.cmd 2>&1 | Select-String "warning|error C|finished"
& python -m unittest discover -s tests 2>&1 | Select-Object -Last 3
```
Expected:
- The build finishes successfully, with no warnings in the `sv_cof_*` files, `sv_client.c` or `sv_save.c`.
- The suite reports `OK`, and the existing party, transition and userinfo harnesses still pass.

- [ ] **Step 7: Commit both repos**

```bash
git -C "$ENGINE" add engine/server/sv_cof_share.h engine/server/sv_cof_share.c engine/server/sv_client.c engine/server/sv_cof_story.c engine/server/sv_save.c
git -C "$ENGINE" commit -m "Share story items between players" -m "Frame and command-scoped inventory diffs feed the ledger; deliveries go
through the game's pickup, used-up copies through inventorydeletenamed.
The ledger resets at session boundaries and survives story transitions.

Co-Authored-By: Claude Opus 5.5 (1M context) <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_018mJcNynXJSSzUadjrVgU5W"
git add tests/engine/cof_share_harness.c tests/test_enhanced_share.py
git commit -m "Test the story item ledger in the engine" -m "Co-Authored-By: Claude Opus 5.5 (1M context) <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_018mJcNynXJSSzUadjrVgU5W"
```

---

### Task 6: Report the item phases

**Files:**
- Modify: `tools/story_run_report.py` (the `PARTY_EVENTS` and `WARN` tuples)
- Modify: `tests/test_story_run_report.py`

**Interfaces:**
- Consumes (from Task 5): the log lines `cofstory phase=item-found|item-used|item-dropped|item-delivered|item-removed|item-deferred …`.
- Produces: counts under "Party events", and `item-deferred` listed as a warning.

- [ ] **Step 1: Extend the failing test**

In `tests/test_story_run_report.py`, add these two lines to the end of the `LOG` string (before the closing `"""`):
```
[2026:09:30|20:03:00] cofstory phase=item-found userid=1 item=inventoryitems/hallkey.txt
[2026:09:30|20:03:01] cofstory phase=item-deferred userid=2 item=inventoryitems/hallkey.txt reason=inventory-full
```
Then add this method to `StoryRunReportTests`:
```python
    def test_item_phases_are_counted_and_deferrals_warned(self):
        with tempfile.TemporaryDirectory() as tmp:
            log = Path(tmp) / 'host.log'
            log.write_text(LOG, encoding='latin-1')
            session = rr.parse_log(log)
        self.assertEqual(session['party']['item-found'], 1)
        self.assertTrue(any('item-deferred' in what for _, what in session['warnings']))
```

- [ ] **Step 2: Run it to see it fail**

Run: `& python -m unittest tests.test_story_run_report -v`
Expected: FAIL in `test_item_phases_are_counted_and_deferrals_warned`, with `0 != 1`.

- [ ] **Step 3: Add the phases**

In `tools/story_run_report.py`, replace the two tuples with:
```python
PARTY_EVENTS = ('exit-leader', 'arrival-placed', 'arrival-kept', 'arrival-overlapped', 'door-party-moved',
                'teleport-party-moved', 'spawn-placed', 'starter-shared', 'door-visibility-restored',
                'item-found', 'item-used', 'item-dropped', 'item-delivered', 'item-removed')
WARN = ('arrival-stranded', 'door-expired', 'teleport-expired', 'door-party-skipped', 'teleport-party-skipped',
        'starter-deferred', 'item-deferred')
```

- [ ] **Step 4: Run it to see it pass**

Run: `& python -m unittest tests.test_story_run_report -v`
Expected: all three tests `ok`. `test_parse_counts_finished_transitions_and_redacts` now sees two warnings, so change its assertion to `self.assertEqual(len(session['warnings']), 2)`.

- [ ] **Step 5: Commit**

```bash
git add tools/story_run_report.py tests/test_story_run_report.py
git commit -m "Count shared-item phases in the run report" -m "Co-Authored-By: Claude Opus 5.5 (1M context) <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_018mJcNynXJSSzUadjrVgU5W"
```

---

### Task 7: Deploy stage 2 and check shared items live

**Files:**
- Modify: `state/enhanced-remote-session.json` (engine hash; untracked)
- Create: `evidence/stage2-share/` (untracked)
- Modify: `AGENT-HANDOFF-2026-09-27.md`

**Interfaces:**
- Consumes (from Task 5): `cof_story_ledger`.
- Consumes (from Task 2): `cof_story_probe`.
- Consumes: `tools/Start-LocalGuest.ps1`, the existing second-copy launcher.
- Produces: the live results, and the go-ahead for the stage 3–4 plan.

- [ ] **Step 1: Suite, stage and deploy**

Run the whole suite (expected `OK`). Then run Task 3 Step 2's deploy script with `$stage = 'evidence\stage2-share'`.
Expected: both runtime `xash.dll` hashes equal the new `engine_sha256`.

- [ ] **Step 2: Solo check — a key survives death**

Tell the user before launching, then start the host as in Task 3 Step 3. Over RCON (`$c` as in Task 3):
```powershell
& $c tools\enhanced_local_control.py "sv_cheats 1" --session remote
& $c tools\enhanced_local_control.py "cof_story_probe give 1 inventoryitems/hallkey.txt" --session remote
& $c tools\enhanced_local_control.py "cof_story_ledger" --session remote --out evidence\stage2-share\01-ledger.log
& $c tools\enhanced_local_control.py "sv_cheats 0" --session remote
```
Expected: `01-ledger.log` contains `ledger-item item=inventoryitems/hallkey.txt finder=1`.

**USER CHECK:** ask the user to open the console (`~`), type `kill`, then respawn when prompted. After respawning, check:
```powershell
& $c tools\enhanced_local_control.py "cof_story_probe slots 1" --session remote --out evidence\stage2-share\02-after-respawn.log
```
Expected:
- `02-after-respawn.log` shows the hall key again.
- The host log shows `cofstory phase=item-delivered userid=1 item=inventoryitems/hallkey.txt`, and no `item-used` line.

- [ ] **Step 3: Two-copy check — found, delivered, used up together**

Start the second copy with `powershell -File tools\Start-LocalGuest.ps1`. Ask the user to run `connect 127.0.0.1:27015` in the guest console (`~`), and wait for both players in `status`. Then run the following, using the host's and guest's userids from `status` (1 and 2 below):
```powershell
& $c tools\enhanced_local_control.py "sv_cheats 1" --session remote
& $c tools\enhanced_local_control.py "cof_story_probe give 1 inventoryitems/hallkey.txt" --session remote
& $c tools\enhanced_local_control.py "cof_story_probe slots 2" --session remote --out evidence\stage2-share\03-guest-after-find.log
& $c tools\enhanced_local_control.py "cof_story_probe remove 1 inventoryitems/hallkey.txt" --session remote
& $c tools\enhanced_local_control.py "cof_story_probe slots 2" --session remote --out evidence\stage2-share\04-guest-after-use.log
& $c tools\enhanced_local_control.py "sv_cheats 0" --session remote
```
Expected:
- `03-guest-after-find.log` shows the hall key.
- `04-guest-after-use.log` does not.
- The host log has `item-found`, `item-delivered`, `item-used` and `item-removed` lines in that order.

**USER CHECK:** in the guest window, did "Little Rascal 1 found the Hall key" appear, followed by "You got the Hall key"? And does the key leave the guest's inventory after the removal?

- [ ] **Step 4: Two-copy check — a drop stays personal**

Using the probe, give the Hall key to player 1. Then ask the user to drop it from the **guest's** inventory screen.
Expected:
- The host keeps the key.
- The guest does not get it back.
- The host log shows `item-dropped userid=2`.

- [ ] **Step 5: Record, report and commit**

Run `& $c tools\story_run_report.py`. It must show the item phases and no PROBLEMS other than historical ones.

Add a dated `## UPDATE` to `AGENT-HANDOFF-2026-09-27.md` with:
- the engine hash;
- the results of Steps 2–4, with evidence file names;
- the next step: "write the stage 3–4 plan (exit hold, warp, door and teleport radius; shared codes, notes and objectives) using the stage 1 results".

```bash
git add AGENT-HANDOFF-2026-09-27.md
git commit -m "Record stage 2 live results for shared story items" -m "Co-Authored-By: Claude Opus 5.5 (1M context) <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_018mJcNynXJSSzUadjrVgU5W"
```
