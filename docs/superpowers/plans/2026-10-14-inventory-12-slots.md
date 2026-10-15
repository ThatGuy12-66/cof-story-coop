# 12-slot story co-op inventory: implementation plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** In story co-op, every player carries 12 inventory items in a 4×3 grid, and every item can be equipped, used, dropped, combined and quick-selected; the items survive level changes, respawns and checkpoints.

**Architecture:** The engine patches the retail `hl.dll` in memory (an always-on "keep" group of 18 NOPs and a 12-slot "wide" group of 11 bound changes), decides each map's slot count at map load, tells clients through a stufftext command, refuses clients without the new screen, and filters out-of-range inventory commands. On the client, the engine redirects `client.dll`'s single slot-string accessor to a 12-slot store and keeps slot 7..12 messages away from `client.dll`. FreeVGUI hides the retail left half of `CRECBInventory` and draws its own grid and item menu, talking to the engine through one new `vguiapi_t` entry. A Python tool generates the grid's background art from the player's own retail images.

**Tech Stack:** C (Xash3D FWGS engine, MSVC x86), C++14 (FreeVGUI, MSVC, no exceptions), Python 3 (tools and `unittest`), PowerShell 7 (test-copy script), waf (`tools\build_enhanced.cmd`).

**Spec:** `docs/superpowers/specs/2026-10-14-inventory-12-slots-design.md`. Research: `evidence/inventory-grid-20261013/server-inventory-re.md`, `client-inventory-re.md`.

## Global Constraints

- Read `AGENTS.md` and `AGENT_NOTES.md` first (project rule).
- Paths: project root `C:\Users\gore\Development\CoF-Coop-Launcher` (call it `ROOT`); engine repository `ROOT\upstream\cof-enhanced\xash3d-fwgs-4857b389e6ba32ddaa68582aedcbc950c138f46a` (call it `ENG`, branch `story-coop`); engine sources `ENG\engine`; FreeVGUI `ENG\3rdparty\freevgui` (its own git repository). These are three separate git repositories.
- Retail files the code is pinned to: `hl.dll` SHA-256 `0036b91c01e92ed205513f52563053a55a66a32d257efb5f476b8f1f3dde0e63`; `client.dll` SHA-256 `d2a04641b301804f6f449aa68265042b13adc360925b80033d417ec9f38c9c00`. Local copies: `ROOT\runtimes\enhanced\cryoffear\cl_dlls\`.
- The disk game DLLs stay stock: patch only in memory, after a hash check, with expected-bytes checks, all-or-nothing, and reverted.
- Code patching compiles only under `XASH_WIN32 && XASH_X86 && defined( XASH_COF_ENTVARS_LEGACY ) && XASH_COF_ENTVARS_LEGACY` (as `sv_cof_story.c`); elsewhere the game stays at six slots.
- Story co-op only. With `cof_story_coop 0`, nothing of this runs and the retail inventory is untouched.
- Never touch `D:\SteamLibrary\steamapps\common\Cry of Fear`, `ROOT\runtimes\enhanced`, `ROOT\runtimes\enhanced-local-client` (read-only use of their DLLs and images is fine). Install only into `ROOT\evidence\story-live-20261012\{host,guest}`. Never launch a game without the user's go-ahead.
- Tests: `python -m unittest discover -s tests` from `ROOT` (a hook blocks `python script.py`; use `python -m ...` or `uv run --no-project python <file>`). Native harnesses need VS 2022 BuildTools at `C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat` (`tests/native.py`).
- Engine C style: tabs, `( a, b )` spacing, `cofstory phase=<name> key=value` log lines, comments at the density of the neighbouring `sv_cof_*.c` files. FreeVGUI: C++14, no exceptions, no STL containers.
- `xash.dll` and `vgui.dll` share `vguiapi_t` and must be installed together from the same build.
- Commits: only if the user has authorized commits for this work (ask once, before Task 1). If authorized, commit each task on a local branch `inventory-12` in every repository the task touches; never push. If not, skip the commit steps and leave the changes in the working trees.
- Do not reset, clean or discard existing work in any of the three repositories.

## Review Focus

- A map that drops back to six slots while items sit in slots 7..12: the keep group must stay applied so the retail clears do not wipe them, and they must come back on the next 12-slot map (Task 2 test "six after twelve").
- An item that disappears under an open menu or a pending combine (a partner used the shared key, the ledger removed it, the story's three-slot section hid it): the menu or the pending action must be cancelled, not sent for an empty slot (Task 7 `Sync` tests).
- `client.dll` loaded away from its preferred base (ASLR): the accessor check and the jump must use the real address (Task 5 accessor test forces a relocation).
- The full 248-character retail userinfo: `_cofinv` must still reach the server, or a 12-slot host turns the player away (Task 4 userinfo test).
- A short, malformed or out-of-range `Inventory` message (slot 0, slot 13+, no terminator): it must never reach `client.dll` (Task 5 store tests).

## File map

| File | Repository | Task | Responsibility |
|---|---|---|---|
| `engine/server/sv_cof_inventory12_patch.h` (new) | ENG | 1 | The hl.dll sites (keep/wide) and the all-or-nothing group writer; pure |
| `engine/server/sv_cof_inventory12.c/.h` (new) | ENG | 2 | Per-map slot count, patching, mode stufftext, join refusal, command filter, status command |
| `engine/server/sv_init.c`, `sv_client.c`, `sv_cof_story.c` | ENG | 2 | Wiring |
| `engine/server/sv_cof_retail.c/.h`, `sv_cof_share_state.h`, `sv_cof_party.c`, `sv_cof_story.c` | ENG | 2, 3 | Data paths carry 12, rules use the active count |
| `engine/common/infostring.c`, `engine/server/sv_cof_cheats.c`, `engine/common/common.h`, `engine/common/dedicated.c` | ENG | 2, 4 | `_cofinv` important key, shared SHA-256, `CL_CoF_Inventory12Ready` |
| `engine/client/cof_inventory12_state.h`, `cof_inventory12_patch.h` (new) | ENG | 5 | Client slot store and message routing; the accessor hook; pure |
| `engine/client/cof_inventory12.c` (new), `engine/vgui_api.h`, `engine/client/vgui/vgui_draw.c`, `cl_main.c`, `parse/cl_parse.c`, `dll_int/cl_game.c`, `client.h` | ENG | 6 | Client glue and the `CofInventory` API |
| `platform/xash3d-fwgs/cofinventory_logic.h` (new) | FreeVGUI | 7 | Grid layout and click rules; pure C++ |
| `platform/xash3d-fwgs/cofinventory.cpp` (new), `app.cpp`, `support.h` | FreeVGUI | 8 | The grid and menu panels |
| `tools/make_inventory12_art.py` (new) | ROOT | 9 | Left-half art from the retail images |
| `tools/Prepare-CampaignTest.ps1`, `docs/campaign-test-guide.md` | ROOT | 10 | Install vgui.dll + art into the test copies; live checklist |
| `tests/engine/cof_inv12_*.c`, `tests/engine/cof_inventory_logic_test.cpp`, `tests/test_enhanced_inventory12.py`, `tests/test_inventory12_art.py`, `tests/native.py` | ROOT | 1–9 | Tests |

---

### Task 1: The hl.dll site table and group writer

**Files:**
- Create: `ENG/engine/server/sv_cof_inventory12_patch.h`
- Create: `ROOT/tests/engine/cof_inv12_patch_harness.c`
- Create: `ROOT/tests/test_enhanced_inventory12.py`

**Interfaces:**
- Produces: `cof_inv12_site_t`, `cof_inv12_write_t`, the tables `cof_inv12_keep[]` (18 sites) and `cof_inv12_wide[]` (11 sites), `COF_INV12_COUNT( table )`, `int CoF_Inv12GroupState( const unsigned char *base, const cof_inv12_site_t *sites, int count )` (1 patched, 0 stock, -1 anything else), `int CoF_Inv12GroupSet( unsigned char *base, const cof_inv12_site_t *sites, int count, int on, cof_inv12_write_t write )` (1 = the whole group is in the asked state). In `tests/test_enhanced_inventory12.py`: `retail( path, sha )`, `run_x86( case, harness, files, args )`, `HL_DLL`, `CLIENT_DLL`, `HL_SHA`, `CLIENT_SHA`.

- [ ] **Step 1: Write the failing test.** Create `ROOT/tests/engine/cof_inv12_patch_harness.c`:

```c
/* The 12-slot inventory sites against the real retail hl.dll, mapped (never run)
 * by the Windows loader: every expected byte is there, a group goes on and off
 * as a whole, and unknown bytes or a failed write leave nothing behind. */
#include <windows.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "patch_under_test.h"

static int writes, fail_at = -1;

static int Write( unsigned char *p, const unsigned char *expect, const unsigned char *value, int len )
{
	DWORD protect, ignored;
	if( writes++ == fail_at ) return 0;
	if( memcmp( p, expect, len ) || !VirtualProtect( p, len, PAGE_EXECUTE_READWRITE, &protect )) return 0;
	memcpy( p, value, len );
	VirtualProtect( p, len, protect, &ignored );
	return 1;
}

static void Poke( unsigned char *p, unsigned char value )
{
	DWORD protect, ignored;
	assert( VirtualProtect( p, 1, PAGE_EXECUTE_READWRITE, &protect ));
	*p = value;
	VirtualProtect( p, 1, protect, &ignored );
}

#define KEEP cof_inv12_keep, COF_INV12_COUNT( cof_inv12_keep )
#define WIDE cof_inv12_wide, COF_INV12_COUNT( cof_inv12_wide )

int main( int argc, char **argv )
{
	unsigned char *base;
	assert( argc == 2 );
	base = (unsigned char *)LoadLibraryExA( argv[1], NULL, DONT_RESOLVE_DLL_REFERENCES );
	assert( base );
	assert( COF_INV12_COUNT( cof_inv12_keep ) == 18 && COF_INV12_COUNT( cof_inv12_wide ) == 11 );
	assert( CoF_Inv12GroupState( base, KEEP ) == 0 && CoF_Inv12GroupState( base, WIDE ) == 0 );

	assert( CoF_Inv12GroupSet( base, KEEP, 1, Write ));
	assert( CoF_Inv12GroupSet( base, WIDE, 1, Write ));
	assert( CoF_Inv12GroupState( base, KEEP ) == 1 && CoF_Inv12GroupState( base, WIDE ) == 1 );
	assert( base[0xDADB1 + 2] == 0x0c ); /* AddInventoryItem: cmp esi,12 */
	assert( base[0xE30C2 + 2] == 0x0b ); /* CombineInventoryItems: slot - 1 <= 11 */
	assert( base[0xDDA3A] == 0x90 && base[0xDDA3A + 10] == 0x90 ); /* models[slot] = 0 is gone */

	writes = 0; /* already there: nothing is written */
	assert( CoF_Inv12GroupSet( base, KEEP, 1, Write ) && writes == 0 );

	/* back to six slots: the keep group stays */
	assert( CoF_Inv12GroupSet( base, WIDE, 0, Write ));
	assert( CoF_Inv12GroupState( base, WIDE ) == 0 && CoF_Inv12GroupState( base, KEEP ) == 1 );

	/* a write that fails half way: the group is rolled back */
	writes = 0; fail_at = 4;
	assert( !CoF_Inv12GroupSet( base, WIDE, 1, Write ));
	assert( CoF_Inv12GroupState( base, WIDE ) == 0 );
	fail_at = -1;

	/* one unknown byte anywhere: refuse before writing anything */
	Poke( base + cof_inv12_wide[5].rva + 2, 0x07 );
	writes = 0;
	assert( !CoF_Inv12GroupSet( base, WIDE, 1, Write ) && writes == 0 );
	assert( CoF_Inv12GroupState( base, WIDE ) == -1 );
	Poke( base + cof_inv12_wide[5].rva + 2, 0x06 );

	assert( CoF_Inv12GroupSet( base, KEEP, 0, Write ) && CoF_Inv12GroupState( base, KEEP ) == 0 );
	FreeLibrary( (HMODULE)base );
	puts( "inventory12 hl.dll sites passed" );
	return 0;
}
```

Create `ROOT/tests/test_enhanced_inventory12.py`:

```python
"""12-slot story co-op inventory: the hl.dll sites, the server rules, the client store and accessor."""
import hashlib
import os
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

from tests.native import ENGINE_SERVER, ROOT, VCVARS, run_native, strip_includes

ENGINE = ENGINE_SERVER.parent
ENGINE_CLIENT = ENGINE / 'client'
HL_DLL = ROOT / 'runtimes/enhanced/cryoffear/cl_dlls/hl.dll'
CLIENT_DLL = ROOT / 'runtimes/enhanced/cryoffear/cl_dlls/client.dll'
HL_SHA = '0036b91c01e92ed205513f52563053a55a66a32d257efb5f476b8f1f3dde0e63'
CLIENT_SHA = 'd2a04641b301804f6f449aa68265042b13adc360925b80033d417ec9f38c9c00'


def retail(path, sha):
    return path.is_file() and hashlib.sha256(path.read_bytes()).hexdigest() == sha


def run_x86(case, harness, files, args=()):
    """MSVC x86 build of a harness that maps a real retail DLL (Windows only)."""
    if os.name != 'nt' or not VCVARS.exists():
        case.skipTest('x86 Windows compiler required: the sites are x86 code in a retail DLL')
    with tempfile.TemporaryDirectory(prefix='cof-inv12-') as tmp:
        work = Path(tmp)
        for name, text in files.items():
            (work / name).write_text(text)
        shutil.copyfile(ROOT / 'tests/engine' / harness, work / 'harness.c')
        quoted = ' '.join(f'"{a}"' for a in args)
        (work / 'build.cmd').write_text(
            f'@call "{VCVARS}" amd64_x86 >nul\n'
            '@cl /nologo /W4 /WX /wd4054 /wd4055 /wd4152 /wd4505 /D_CRT_SECURE_NO_WARNINGS /TC harness.c /Fe:inv12-test.exe\n'
            '@if errorlevel 1 exit /b %errorlevel%\n'
            f'@.\\inv12-test.exe {quoted}\n')
        result = subprocess.run(['cmd.exe', '/d', '/c', str(work / 'build.cmd')], cwd=work,
                                capture_output=True, text=True, timeout=120)
        case.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        return result.stdout


class HlSiteTests(unittest.TestCase):
    def test_sites_against_retail_hl(self):
        if not retail(HL_DLL, HL_SHA):
            self.skipTest('the retail Cry of Fear 1.6 hl.dll is not installed locally')
        out = run_x86(self, 'cof_inv12_patch_harness.c',
                      {'patch_under_test.h': (ENGINE_SERVER / 'sv_cof_inventory12_patch.h').read_text()}, [HL_DLL])
        self.assertIn('inventory12 hl.dll sites passed', out)


if __name__ == '__main__':
    unittest.main()
```

- [ ] **Step 2: Run it to see it fail.** `python -m unittest tests.test_enhanced_inventory12 -v` from `ROOT`. Expected: FAIL (`FileNotFoundError` for `sv_cof_inventory12_patch.h`).

- [ ] **Step 3: Write the table and the writer.** Create `ENG/engine/server/sv_cof_inventory12_patch.h` (the bytes come from `evidence/inventory-grid-20261013/server-inventory-re.md` §5; each was read in the disassembly):

```c
/* sv_cof_inventory12_patch.h - Cry of Fear story co-op: the retail hl.dll sites
 * of the 12-slot inventory. GPL-3.0-or-later.
 *
 * Pure: no engine types. Included by sv_cof_inventory12.c and by the offline
 * harness, which runs it against the real DLL. Cry of Fear 1.6 hl.dll, sha256
 * 0036B91C...0E63; addresses are RVAs (VA - 0x10000000). The disassembly behind
 * every site: evidence/inventory-grid-20261013/server-inventory-re.md.
 *
 * inventorySlot[7] (+0x21FC) grows in place over the arrays after it, which the
 * game only ever zeroes: models[7] (+0x2218), classnames[7] (+0x2234) and the
 * unsaved client copy (+0x2250). Slots 7..12 sit on models[0..5].
 */
#ifndef SV_COF_INVENTORY12_PATCH_H
#define SV_COF_INVENTORY12_PATCH_H
#include <string.h>

#define COF_INV12_SITE_MAX 11
#define COF_INV12_COUNT( table ) ((int)( sizeof( table ) / sizeof(( table )[0] )))

typedef struct
{
	unsigned int rva;
	int len;
	unsigned char stock[COF_INV12_SITE_MAX];
	unsigned char patched[COF_INV12_SITE_MAX];
} cof_inv12_site_t;

/* Compares p with expect, then writes value; len bytes. Nonzero on success. */
typedef int (*cof_inv12_write_t)( unsigned char *p, const unsigned char *expect, const unsigned char *value, int len );

#define COF_INV12_NOPS { 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90 }
#define COF_INV12_ESI   { 0xc7, 0x06, 0x00, 0x00, 0x00, 0x00 }             /* mov [esi], 0: models */
#define COF_INV12_ESI1C { 0xc7, 0x46, 0x1c, 0x00, 0x00, 0x00, 0x00 }       /* mov [esi+1Ch], 0: classnames */
#define COF_INV12_ESI38 { 0xc7, 0x46, 0x38, 0x00, 0x00, 0x00, 0x00 }       /* mov [esi+38h], 0: client copy */

/* keep: the zero-writes into the dead arrays. Applied whenever story co-op runs,
 * at six slots too: ClearInventorySlot's bAll loops write models[i], which is
 * slot i + 7, so a map at six slots would otherwise wipe slots 8..13. */
static const cof_inv12_site_t cof_inv12_keep[] =
{
	{ 0xDD9C4, 6, COF_INV12_ESI, COF_INV12_NOPS },   /* ClearInventorySlot bAll, three-slot loop */
	{ 0xDD9CA, 7, COF_INV12_ESI1C, COF_INV12_NOPS },
	{ 0xDD9D1, 7, COF_INV12_ESI38, COF_INV12_NOPS },
	{ 0xDD9FE, 6, COF_INV12_ESI, COF_INV12_NOPS },   /* ClearInventorySlot bAll, six-slot loop */
	{ 0xDDA04, 7, COF_INV12_ESI1C, COF_INV12_NOPS },
	{ 0xDDA0B, 7, COF_INV12_ESI38, COF_INV12_NOPS },
	{ 0xDDA3A, 11, { 0xc7, 0x84, 0x87, 0x18, 0x22, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }, COF_INV12_NOPS }, /* ClearInventorySlot one slot */
	{ 0xDDA45, 11, { 0xc7, 0x84, 0x87, 0x34, 0x22, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }, COF_INV12_NOPS },
	{ 0xDDA50, 11, { 0xc7, 0x84, 0x87, 0x50, 0x22, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }, COF_INV12_NOPS },
	{ 0xDDACE, 6, COF_INV12_ESI, COF_INV12_NOPS },   /* DeleteInventoryItemNamed bAll, three-slot loop */
	{ 0xDDAD4, 7, COF_INV12_ESI1C, COF_INV12_NOPS },
	{ 0xDDADB, 7, COF_INV12_ESI38, COF_INV12_NOPS },
	{ 0xDDB0E, 6, COF_INV12_ESI, COF_INV12_NOPS },   /* DeleteInventoryItemNamed bAll, six-slot loop */
	{ 0xDDB14, 7, COF_INV12_ESI1C, COF_INV12_NOPS },
	{ 0xDDB1B, 7, COF_INV12_ESI38, COF_INV12_NOPS },
	{ 0xDDC0E, 11, { 0xc7, 0x84, 0xb3, 0x18, 0x22, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }, COF_INV12_NOPS }, /* DeleteInventoryItemNamed one slot */
	{ 0xDDC19, 11, { 0xc7, 0x84, 0xb3, 0x34, 0x22, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }, COF_INV12_NOPS },
	{ 0xDDC25, 11, { 0xc7, 0x84, 0xb3, 0x50, 0x22, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }, COF_INV12_NOPS },
};

/* wide: the slot loops and the combine range, 6 -> 12. Only on maps at 12.
 * Not here: SavePlayer 1002A866 (its coop record has six fixed weapon entries;
 * story co-op carries inventories through the engine's own save paths) and
 * DualWield (refused in multiplayer before its range check). */
static const cof_inv12_site_t cof_inv12_wide[] =
{
	{ 0xDADB1, 3, { 0x83, 0xfe, 0x06 }, { 0x83, 0xfe, 0x0c } }, /* AddInventoryItem: the first free slot */
	{ 0xDDA1B, 3, { 0x83, 0xfb, 0x06 }, { 0x83, 0xfb, 0x0c } }, /* ClearInventorySlot bAll */
	{ 0xDDB2B, 3, { 0x83, 0xff, 0x06 }, { 0x83, 0xff, 0x0c } }, /* DeleteInventoryItemNamed bAll */
	{ 0xDDBF3, 3, { 0x83, 0xfe, 0x06 }, { 0x83, 0xfe, 0x0c } }, /* DeleteInventoryItemNamed find */
	{ 0xE22ED, 3, { 0x83, 0xfe, 0x06 }, { 0x83, 0xfe, 0x0c } }, /* HasInventoryItem */
	{ 0xE4341, 3, { 0x83, 0xff, 0x06 }, { 0x83, 0xff, 0x0c } }, /* IsInventoryFull */
	{ 0xF75A0, 3, { 0x83, 0xff, 0x06 }, { 0x83, 0xff, 0x0c } }, /* UpdateClientData: the InitHUD resend */
	{ 0xC64D0, 3, { 0x83, 0xfe, 0x06 }, { 0x83, 0xfe, 0x0c } }, /* coop file loader resend */
	{ 0xC6D70, 3, { 0x83, 0xfe, 0x06 }, { 0x83, 0xfe, 0x0c } }, /* map .cfg item loader resend */
	{ 0xE30C2, 3, { 0x83, 0xf8, 0x05 }, { 0x83, 0xf8, 0x0b } }, /* CombineInventoryItems: first slot in 1..N */
	{ 0xE30CE, 3, { 0x83, 0xf8, 0x05 }, { 0x83, 0xf8, 0x0b } }, /* second slot in 1..N */
};

/* 1 when every site holds its patched bytes, 0 when every site holds the stock
 * bytes, -1 for anything else. */
static int CoF_Inv12GroupState( const unsigned char *base, const cof_inv12_site_t *sites, int count )
{
	int i, stock = 0, patched = 0;
	for( i = 0; i < count; i++ )
	{
		if( !memcmp( base + sites[i].rva, sites[i].stock, sites[i].len )) stock++;
		else if( !memcmp( base + sites[i].rva, sites[i].patched, sites[i].len )) patched++;
	}
	return stock == count ? 0 : patched == count ? 1 : -1;
}

/* Puts one group (at most 32 sites) into its patched (on) or stock state, every
 * site or none: it refuses unless every site holds one of its two known byte
 * strings, and undoes its own writes when one fails. 1 on success. */
static int CoF_Inv12GroupSet( unsigned char *base, const cof_inv12_site_t *sites, int count, int on, cof_inv12_write_t write )
{
	unsigned int written = 0;
	int i, j;
	for( i = 0; i < count; i++ )
		if( memcmp( base + sites[i].rva, sites[i].stock, sites[i].len ) && memcmp( base + sites[i].rva, sites[i].patched, sites[i].len ))
			return 0;
	for( i = 0; i < count; i++ )
	{
		const unsigned char *from = on ? sites[i].stock : sites[i].patched;
		const unsigned char *to = on ? sites[i].patched : sites[i].stock;
		if( !memcmp( base + sites[i].rva, to, sites[i].len )) continue;
		if( !write( base + sites[i].rva, from, to, sites[i].len ))
		{
			for( j = 0; j < i; j++ )
				if( written & ( 1u << j ))
					write( base + sites[j].rva, on ? sites[j].patched : sites[j].stock, on ? sites[j].stock : sites[j].patched, sites[j].len );
			return 0;
		}
		written |= 1u << i;
	}
	return 1;
}

#endif /* SV_COF_INVENTORY12_PATCH_H */
```

- [ ] **Step 4: Run it to see it pass.** `python -m unittest tests.test_enhanced_inventory12 -v`. Expected: `test_sites_against_retail_hl ... ok` (or `skipped` only where the retail hl.dll is absent; on this machine it is present and must pass).

- [ ] **Step 5: Commit** (see Global Constraints). Engine: `git -C ENG add engine/server/sv_cof_inventory12_patch.h && git -C ENG commit -m "Story co-op: the retail hl.dll sites of the 12-slot inventory"`. Launcher: `git add tests/engine/cof_inv12_patch_harness.c tests/test_enhanced_inventory12.py && git commit -m "Test the 12-slot inventory sites against the retail hl.dll"`.

---

### Task 2: Server rules: per-map slot count, mode command, join refusal, command filter

**Files:**
- Create: `ENG/engine/server/sv_cof_inventory12.h`, `ENG/engine/server/sv_cof_inventory12.c`
- Modify: `ENG/engine/server/sv_cof_retail.h`, `sv_cof_retail.c` (one getter), `sv_init.c` (~line 599), `sv_client.c` (`SV_ConnectClient` ~line 416, `SV_ExecuteClientCommand` ~line 3150), `sv_cof_story.c` (`CoF_StoryCloseGate`, `SV_CoF_StoryInit`, `SV_CoF_StoryFrame`), `ENG/engine/common/common.h` (~line 779), `ENG/engine/common/dedicated.c`
- Create: `ROOT/tests/engine/cof_inv12_server_harness.c`
- Modify: `ROOT/tests/test_enhanced_inventory12.py`

**Interfaces:**
- Consumes (Task 1): `cof_inv12_keep`, `cof_inv12_wide`, `COF_INV12_COUNT`, `CoF_Inv12GroupSet`, `CoF_Inv12GroupState`. Existing: `SV_CoF_StoryCoopEnabled()`, `uintptr_t SV_CoF_StoryBase( void )` (`sv_cof_story.c:155`, 0 unless the gate is open).
- Produces: `#define COF_INV12_USERINFO "_cofinv"`; `int SV_CoF_InventorySlots( void )` (12 or 6); `void SV_CoF_Inventory12Init( void )`, `void SV_CoF_Inventory12Activate( void )`, `void SV_CoF_Inventory12Close( void )`, `void SV_CoF_Inventory12Frame( void )`, `qboolean SV_CoF_Inventory12Refuse( const char *userinfo, qboolean local )`, `qboolean SV_CoF_Inventory12ClientCommand( sv_client_t *cl )`; `qboolean SV_CoF_RetailInThreeSlots( sv_client_t *cl )`; `qboolean CL_CoF_Inventory12Ready( void )` (declared in `common.h`; implemented in Task 6, stub in `dedicated.c`). Server cvar `cof_inventory_slots` (default 12), command `cof_inventory12_status`, client command sent `cof_inv_slots <12|6>`, log phases `inventory12`, `inventory12-off reason=<cvar|host-client|client:<userid>|bytes|platform>`, `inventory12-join-refused`, `inventory-cmd-refused`, `inventory12-status`.
- Note: until Task 6 provides `CL_CoF_Inventory12Ready`, the full engine does not link; the offline tests do not need it.

- [ ] **Step 1: Write the failing test.** Create `ROOT/tests/engine/cof_inv12_server_harness.c`:

```c
/* The 12-slot inventory's server rules (sv_cof_inventory12.c) against engine
 * doubles and a fake hl.dll image: the per-map slot count, the keep and wide
 * groups, the cof_inv_slots stufftext, join refusal and the command filter. */
#include <assert.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef unsigned char byte;
typedef int qboolean;
#define true 1
#define false 0
#define MAX_CLIENTS 4
#define cs_free 0
#define cs_connected 2
#define cs_spawned 4
#define ss_loading 1
#define ss_active 2
#define FCL_FAKECLIENT (1<<7)
#define svc_stufftext 9
#define FBitSet(f,b) ((f)&(b))
#define Q_strcmp strcmp
#define Q_stricmp _stricmp
#define Q_atoi atoi
#define Host_IsDedicated() ( dedicated )
#define COF_INV12_USERINFO "_cofinv" /* sv_cof_inventory12.h */
typedef struct { float value; const char *string; } convar_t;
typedef struct { char text[512]; } sizebuf_t;
typedef struct { sizebuf_t message; } netchan_t;
typedef struct { int state, userid; unsigned int flags; char userinfo[256]; netchan_t netchan; } sv_client_t;
static struct { int state; } sv;
static struct { int maxclients, spawncount; sv_client_t *clients; } svs;
static struct { void *hInstance; } svgame;
static sv_client_t clients[MAX_CLIENTS];
static convar_t slots_cvar = { 12, "12" };
static int story = 1, dedicated, host_ready = 1, three, written;
static unsigned char image[0x100000];
static char log_text[8192];
static size_t log_length;
static void (*status_f)( void );
static const char *args[4];
static int argc;

static void Q_strncpy( char *d, const char *s, size_t n ) { size_t i; for( i = 0; i + 1 < n && s[i]; i++ ) d[i] = s[i]; d[i] = 0; }
static void Con_Printf( const char *fmt, ... )
{
	va_list a; int n;
	va_start( a, fmt );
	n = vsnprintf( log_text + log_length, sizeof( log_text ) - log_length, fmt, a );
	va_end( a );
	assert( n >= 0 && (size_t)n < sizeof( log_text ) - log_length );
	log_length += (size_t)n;
}
static char *va( const char *fmt, ... )
{
	static char buf[128]; va_list a;
	va_start( a, fmt ); vsnprintf( buf, sizeof( buf ), fmt, a ); va_end( a );
	return buf;
}
static convar_t *Cvar_Get( const char *name, const char *value, int flags, const char *description )
{
	assert( !strcmp( name, "cof_inventory_slots" ) && !strcmp( value, "12" ));
	return &slots_cvar;
}
static void Cmd_AddCommand( const char *name, void (*f)( void ), const char *description )
{
	assert( !strcmp( name, "cof_inventory12_status" ));
	status_f = f;
}
static const char *Cmd_Argv( int i ) { return i < argc ? args[i] : ""; }
static const char *Info_ValueForKey( const char *s, const char *key )
{
	static char value[64];
	char k[64];
	while( *s == '\\' )
	{
		int n = 0;
		for( s++; *s && *s != '\\' && n < 63; ) k[n++] = *s++;
		k[n] = 0;
		if( *s != '\\' ) break;
		for( s++, n = 0; *s && *s != '\\' && n < 63; ) value[n++] = *s++;
		value[n] = 0;
		if( !strcmp( k, key )) return value;
	}
	value[0] = 0;
	return value;
}
static qboolean SV_CoF_StoryCoopEnabled( void ) { return story; }
static uintptr_t SV_CoF_StoryBase( void ) { return story ? (uintptr_t)image : 0; }
static qboolean CL_CoF_Inventory12Ready( void ) { return host_ready; }
static qboolean SV_CoF_RetailInThreeSlots( sv_client_t *cl ) { return three; }
static void MSG_BeginServerCmd( sizebuf_t *msg, int cmd ) { assert( cmd == svc_stufftext ); }
static void MSG_WriteString( sizebuf_t *msg, const char *s ) { strcat( msg->text, s ); }
static int Fake( unsigned char *p, const unsigned char *expect, const unsigned char *value, int len )
{
	if( memcmp( p, expect, len )) return 0;
	memcpy( p, value, len );
	written++;
	return 1;
}
#define COF_INV12_WRITE Fake

#include "patch_under_test.h"
#include "inventory12_under_test.c"

#define KEEP_STATE CoF_Inv12GroupState( image, cof_inv12_keep, COF_INV12_COUNT( cof_inv12_keep ))
#define WIDE_STATE CoF_Inv12GroupState( image, cof_inv12_wide, COF_INV12_COUNT( cof_inv12_wide ))

static int logged( const char *text )
{
	int n = 0; const char *p = log_text;
	while(( p = strstr( p, text ))) { n++; p += strlen( text ); }
	return n;
}
static void stock_image( void )
{
	int i;
	memset( image, 0xcc, sizeof( image ));
	for( i = 0; i < COF_INV12_COUNT( cof_inv12_keep ); i++ ) memcpy( image + cof_inv12_keep[i].rva, cof_inv12_keep[i].stock, cof_inv12_keep[i].len );
	for( i = 0; i < COF_INV12_COUNT( cof_inv12_wide ); i++ ) memcpy( image + cof_inv12_wide[i].rva, cof_inv12_wide[i].stock, cof_inv12_wide[i].len );
}
static void person( int i, int userid, const char *userinfo )
{
	clients[i].state = cs_spawned; clients[i].userid = userid; clients[i].flags = 0;
	strcpy( clients[i].userinfo, userinfo ); clients[i].netchan.message.text[0] = 0;
}
static void map( void ) { svs.spawncount++; sv.state = ss_loading; SV_CoF_Inventory12Activate(); sv.state = ss_active; }
static qboolean command( const char *a0, const char *a1, const char *a2 )
{
	args[0] = a0; args[1] = a1; args[2] = a2; argc = a2 ? 3 : a1 ? 2 : 1;
	return SV_CoF_Inventory12ClientCommand( &clients[0] );
}

int main( void )
{
	svs.maxclients = 2; svs.clients = clients; svgame.hInstance = (void *)1;
	stock_image();
	SV_CoF_Inventory12Init();
	assert( SV_CoF_InventorySlots() == 6 ); /* nothing before the first map */

	/* a listen host whose own client has the grid, nobody connected yet: 12 */
	map();
	assert( SV_CoF_InventorySlots() == 12 && KEEP_STATE == 1 && WIDE_STATE == 1 );
	assert( logged( "cofstory phase=inventory12 slots=12\n" ) == 1 );

	/* every person is told once per map; the AI companion never */
	person( 0, 1, "\\name\\host\\_cofinv\\12" );
	SV_CoF_Inventory12Frame();
	SV_CoF_Inventory12Frame();
	assert( !strcmp( clients[0].netchan.message.text, "cof_inv_slots 12\n" ));
	clients[1].state = cs_connected; clients[1].flags = FCL_FAKECLIENT;
	SV_CoF_Inventory12Frame();
	assert( !clients[1].netchan.message.text[0] );
	clients[1].state = cs_free; clients[1].flags = 0;

	/* joining a map at 12 needs the 12-slot screen; the host's own client is never refused */
	assert( SV_CoF_Inventory12Refuse( "\\name\\old", false ));
	assert( logged( "cofstory phase=inventory12-join-refused\n" ) == 1 );
	assert( !SV_CoF_Inventory12Refuse( "\\name\\new\\_cofinv\\12", false ));
	assert( !SV_CoF_Inventory12Refuse( "\\name\\old", true ));

	/* the command filter at 12 */
	assert( !command( "inventoryequip", "12", NULL ));
	assert( command( "inventoryequip", "13", NULL ));
	assert( command( "inventorydrop", "0", NULL ));
	assert( command( "inventoryuse", "7x", NULL ));
	assert( command( "inventoryequip", NULL, NULL ));
	assert( !command( "inventorycombine", "6", "7" ));
	assert( command( "inventorycombine", "6", "13" ));
	assert( command( "inventorydualwield", "13", "2" ));
	assert( !command( "quickselset2", "9", NULL ));
	assert( !command( "inventorydeletenamed", "inventoryitems/x.txt", "0" )); /* not a slot number */
	assert( logged( "cofstory phase=inventory-cmd-refused userid=1 cmd=inventoryequip args=\"13 \"\n" ) == 1 );
	three = 1; /* the story's three-slot sections */
	assert( command( "inventoryequip", "4", NULL ));
	assert( !command( "inventoryequip", "3", NULL ));
	three = 0;

	/* a person without the screen connected at map load: six, the keep group stays */
	person( 1, 2, "\\name\\old" );
	map();
	assert( SV_CoF_InventorySlots() == 6 && KEEP_STATE == 1 && WIDE_STATE == 0 );
	assert( logged( "cofstory phase=inventory12-off reason=client:2\n" ) == 1 );
	SV_CoF_Inventory12Frame();
	assert( strstr( clients[0].netchan.message.text, "cof_inv_slots 6\n" ));
	assert( command( "inventoryequip", "7", NULL ));
	assert( !SV_CoF_Inventory12Refuse( "\\name\\old", false )); /* a map at six takes anyone */

	/* six after twelve: 12 again on the next map once everyone has the screen */
	clients[1].state = cs_free;
	map();
	assert( SV_CoF_InventorySlots() == 12 && WIDE_STATE == 1 );

	/* the cvar, a host client without the grid, a dedicated server */
	slots_cvar.value = 6; map();
	assert( SV_CoF_InventorySlots() == 6 && logged( "reason=cvar\n" ) == 1 );
	slots_cvar.value = 12; host_ready = 0; map();
	assert( SV_CoF_InventorySlots() == 6 && logged( "reason=host-client\n" ) == 1 );
	dedicated = 1; map();
	assert( SV_CoF_InventorySlots() == 12 );
	dedicated = 0; host_ready = 1;

	status_f();
	assert( logged( "cofstory phase=inventory12-status slots=12 cvar=12 keep=1 wide=1 reason=none\n" ) == 1 );

	/* story co-op off: the gate's close puts every byte back; nothing is filtered */
	SV_CoF_Inventory12Close();
	assert( SV_CoF_InventorySlots() == 6 && KEEP_STATE == 0 && WIDE_STATE == 0 );
	story = 0; map();
	assert( SV_CoF_InventorySlots() == 6 && KEEP_STATE == 0 );
	assert( !command( "inventoryequip", "13", NULL ));
	story = 1;

	/* unknown bytes: six, nothing written */
	image[cof_inv12_keep[3].rva] = 0x90;
	written = 0; map();
	assert( SV_CoF_InventorySlots() == 6 && written == 0 && logged( "reason=bytes\n" ) == 1 );
	puts( "inventory12 server rules passed" );
	return 0;
}
```

Add to `ROOT/tests/test_enhanced_inventory12.py` (above `if __name__`):

```python
class ServerRuleTests(unittest.TestCase):
    def test_rules(self):
        header = (ENGINE_SERVER / 'sv_cof_inventory12.h').read_text()
        self.assertIn('#define COF_INV12_USERINFO "_cofinv"', header)  # the harness repeats it
        out = run_native(self, 'cof_inv12_server_harness.c', {
            'patch_under_test.h': (ENGINE_SERVER / 'sv_cof_inventory12_patch.h').read_text(),
            'inventory12_under_test.c': strip_includes((ENGINE_SERVER / 'sv_cof_inventory12.c').read_text()),
        })
        self.assertIn('inventory12 server rules passed', out)
```

- [ ] **Step 2: Run it to see it fail.** `python -m unittest tests.test_enhanced_inventory12 -v`. Expected: `test_rules` FAIL (`FileNotFoundError: sv_cof_inventory12.h`).

- [ ] **Step 3: Write the header.** Create `ENG/engine/server/sv_cof_inventory12.h`:

```c
/*
sv_cof_inventory12.h - Cry of Fear story co-op: the 12-slot inventory, server side
GPL-3.0-or-later; docs/superpowers/specs/2026-10-14-inventory-12-slots-design.md
*/
#ifndef SV_COF_INVENTORY12_H
#define SV_COF_INVENTORY12_H

#define COF_INV12_USERINFO "_cofinv" // a client with the 12-slot screen sends "12"

void SV_CoF_Inventory12Init( void );
// sv_init.c SV_ActivateServer, before the game's ServerActivate: this map's
// slot count, and the hl.dll bytes that go with it.
void SV_CoF_Inventory12Activate( void );
// The story gate closes (story co-op off, the game DLL going away): stock bytes.
void SV_CoF_Inventory12Close( void );
// Story frame: tells every person connected the map's slot count, once per map.
void SV_CoF_Inventory12Frame( void );
// 12 while this map runs the 12-slot inventory, else 6 (the retail inventory).
int SV_CoF_InventorySlots( void );
// SV_ConnectClient: true when this client must be turned away (a map at 12 and
// no 12-slot screen: its client.dll would crash on the first slot 7 message).
qboolean SV_CoF_Inventory12Refuse( const char *userinfo, qboolean local );
// SV_ExecuteClientCommand: an inventory command whose slot number is out of
// range is dropped (the retail handlers do not check). True when consumed.
qboolean SV_CoF_Inventory12ClientCommand( sv_client_t *cl );

#endif // SV_COF_INVENTORY12_H
```

- [ ] **Step 4: Write the module.** Create `ENG/engine/server/sv_cof_inventory12.c`:

```c
/*
sv_cof_inventory12.c - Cry of Fear story co-op: the 12-slot inventory, server side
GPL-3.0-or-later; docs/superpowers/specs/2026-10-14-inventory-12-slots-design.md section 1.

The disk game DLL stays stock. After the story gate's file hash check the keep
group (sv_cof_inventory12_patch.h) is applied whenever story co-op runs, and the
wide group only on a map that runs at 12 slots: the cvar asks for it, the bytes
are the expected ones, and every person connected at map load - the listen
host's own client too - has the 12-slot screen. A person who joins a map at 12
needs it as well, or is turned away: a retail client.dll crashes on slot 7.
*/
#include "common.h"
#include "server.h"
#include "sv_cof_story.h"
#include "sv_cof_retail.h"
#include "sv_cof_inventory12.h"
#include "sv_cof_inventory12_patch.h"
#if XASH_WIN32
#include <windows.h>
#endif

static struct
{
	convar_t *cvar;            // cof_inventory_slots
	unsigned char *base;       // the hl.dll image the bytes were written in, NULL for none
	void *instance;            // the svgame.hInstance of that image
	int slots;                 // this map: 12 or 6
	char reason[48];           // why this map runs at six, "" at 12
	struct { int map, slots; } told[MAX_CLIENTS]; // what each client slot was last told
} cof_inv12;

#ifndef COF_INV12_WRITE
static int CoF_Inv12WriteCode( unsigned char *p, const unsigned char *expect, const unsigned char *value, int len )
{
#if XASH_WIN32 && XASH_X86 && defined( XASH_COF_ENTVARS_LEGACY ) && XASH_COF_ENTVARS_LEGACY
	DWORD protect, ignored;
	if( memcmp( p, expect, len ) || !VirtualProtect( p, len, PAGE_EXECUTE_READWRITE, &protect )) return 0;
	memcpy( p, value, len );
	FlushInstructionCache( GetCurrentProcess(), p, len );
	VirtualProtect( p, len, protect, &ignored );
	return 1;
#else
	return 0;
#endif
}
#define COF_INV12_WRITE CoF_Inv12WriteCode
#endif

#define COF_INV12_KEEP cof_inv12_keep, COF_INV12_COUNT( cof_inv12_keep )
#define COF_INV12_WIDE cof_inv12_wide, COF_INV12_COUNT( cof_inv12_wide )

int SV_CoF_InventorySlots( void )
{
	return cof_inv12.base && cof_inv12.slots == 12 ? 12 : 6;
}

void SV_CoF_Inventory12Close( void )
{
	if( cof_inv12.base && cof_inv12.instance == svgame.hInstance )
	{
		CoF_Inv12GroupSet( cof_inv12.base, COF_INV12_WIDE, 0, COF_INV12_WRITE );
		CoF_Inv12GroupSet( cof_inv12.base, COF_INV12_KEEP, 0, COF_INV12_WRITE );
	}
	cof_inv12.base = NULL;
	cof_inv12.instance = NULL;
	cof_inv12.slots = 6;
}

static qboolean CoF_Inv12Person( const sv_client_t *cl )
{
	return cl->state >= cs_connected && !FBitSet( cl->flags, FCL_FAKECLIENT );
}

static qboolean CoF_Inv12HasScreen( const char *userinfo )
{
	return !Q_strcmp( Info_ValueForKey( userinfo, COF_INV12_USERINFO ), "12" );
}

static void CoF_Inv12Six( const char *reason )
{
	Q_strncpy( cof_inv12.reason, reason, sizeof( cof_inv12.reason ));
	cof_inv12.slots = 6;
	Con_Printf( "cofstory phase=inventory12-off reason=%s\n", reason );
}

void SV_CoF_Inventory12Activate( void )
{
	unsigned char *base;
	const char *reason = NULL;
	int i;
	cof_inv12.slots = 6;
	cof_inv12.reason[0] = 0;
	if( !SV_CoF_StoryCoopEnabled( )) return; // closing the gate already put the stock bytes back
	base = (unsigned char *)SV_CoF_StoryBase( );
	if( cof_inv12.base && ( cof_inv12.base != base || cof_inv12.instance != svgame.hInstance ))
		cof_inv12.base = NULL; // another image: the old one is gone with its DLL
	if( !base )
	{
		CoF_Inv12Six( "platform" );
		return;
	}
	// keep at six slots too, or the retail clears wipe slots 8..13
	if( !CoF_Inv12GroupSet( base, COF_INV12_KEEP, 1, COF_INV12_WRITE ))
	{
		cof_inv12.base = NULL;
		CoF_Inv12Six( "bytes" );
		return;
	}
	cof_inv12.base = base;
	cof_inv12.instance = svgame.hInstance;
	if( !cof_inv12.cvar || (int)cof_inv12.cvar->value != 12 ) reason = "cvar";
	else if( !Host_IsDedicated( ) && !CL_CoF_Inventory12Ready( )) reason = "host-client";
	for( i = 0; !reason && i < svs.maxclients && i < MAX_CLIENTS; i++ )
		if( CoF_Inv12Person( &svs.clients[i] ) && !CoF_Inv12HasScreen( svs.clients[i].userinfo ))
			reason = va( "client:%d", svs.clients[i].userid );
	if( reason )
	{
		CoF_Inv12GroupSet( base, COF_INV12_WIDE, 0, COF_INV12_WRITE );
		CoF_Inv12Six( reason );
		return;
	}
	if( !CoF_Inv12GroupSet( base, COF_INV12_WIDE, 1, COF_INV12_WRITE ))
	{
		CoF_Inv12Six( "bytes" );
		return;
	}
	cof_inv12.slots = 12;
	Con_Printf( "cofstory phase=inventory12 slots=12\n" );
}

void SV_CoF_Inventory12Frame( void )
{
	int i;
	if( sv.state != ss_active || !SV_CoF_StoryCoopEnabled( )) return;
	for( i = 0; i < svs.maxclients && i < MAX_CLIENTS; i++ )
	{
		sv_client_t *cl = &svs.clients[i];
		if( !CoF_Inv12Person( cl ))
		{
			cof_inv12.told[i].map = 0;
			continue;
		}
		if( cof_inv12.told[i].map == svs.spawncount && cof_inv12.told[i].slots == SV_CoF_InventorySlots( )) continue;
		MSG_BeginServerCmd( &cl->netchan.message, svc_stufftext );
		MSG_WriteString( &cl->netchan.message, va( "cof_inv_slots %d\n", SV_CoF_InventorySlots( )));
		cof_inv12.told[i].map = svs.spawncount;
		cof_inv12.told[i].slots = SV_CoF_InventorySlots( );
	}
}

qboolean SV_CoF_Inventory12Refuse( const char *userinfo, qboolean local )
{
	if( local || sv.state != ss_active || SV_CoF_InventorySlots( ) != 12 || CoF_Inv12HasScreen( userinfo )) return false;
	Con_Printf( "cofstory phase=inventory12-join-refused\n" );
	return true;
}

// A slot number argument: digits only, 1..limit.
static qboolean CoF_Inv12SlotArg( int index, int limit )
{
	const char *s = Cmd_Argv( index );
	int i;
	if( !s[0] ) return false;
	for( i = 0; s[i]; i++ )
		if( s[i] < '0' || s[i] > '9' ) return false;
	i = Q_atoi( s );
	return i >= 1 && i <= limit;
}

qboolean SV_CoF_Inventory12ClientCommand( sv_client_t *cl )
{
	const char *cmd = Cmd_Argv( 0 );
	qboolean one, two;
	int limit;
	one = !Q_stricmp( cmd, "inventoryuse" ) || !Q_stricmp( cmd, "inventoryequip" ) || !Q_stricmp( cmd, "inventorydrop" ) ||
		!Q_stricmp( cmd, "quickselset1" ) || !Q_stricmp( cmd, "quickselset2" ) || !Q_stricmp( cmd, "quickselset3" );
	two = !Q_stricmp( cmd, "inventorycombine" ) || !Q_stricmp( cmd, "inventorydualwield" );
	if(( !one && !two ) || !SV_CoF_StoryCoopEnabled( )) return false;
	limit = SV_CoF_RetailInThreeSlots( cl ) ? 3 : SV_CoF_InventorySlots( );
	if( CoF_Inv12SlotArg( 1, limit ) && ( one || CoF_Inv12SlotArg( 2, limit ))) return false;
	Con_Printf( "cofstory phase=inventory-cmd-refused userid=%d cmd=%s args=\"%s %s\"\n", cl->userid, cmd, Cmd_Argv( 1 ), Cmd_Argv( 2 ));
	return true;
}

static void CoF_Inv12Status_f( void )
{
	Con_Printf( "cofstory phase=inventory12-status slots=%d cvar=%d keep=%d wide=%d reason=%s\n", SV_CoF_InventorySlots( ),
		cof_inv12.cvar ? (int)cof_inv12.cvar->value : 0,
		cof_inv12.base ? CoF_Inv12GroupState( cof_inv12.base, COF_INV12_KEEP ) : 0,
		cof_inv12.base ? CoF_Inv12GroupState( cof_inv12.base, COF_INV12_WIDE ) : 0,
		cof_inv12.reason[0] ? cof_inv12.reason : "none" );
}

void SV_CoF_Inventory12Init( void )
{
	cof_inv12.cvar = Cvar_Get( "cof_inventory_slots", "12", 0, "story co-op inventory slots: 12, or 6 for the retail inventory (from the next map)" );
	cof_inv12.slots = 6;
	Cmd_AddCommand( "cof_inventory12_status", CoF_Inv12Status_f, "story co-op: the 12-slot inventory's state" );
}
```

- [ ] **Step 5: The retail getter.** In `ENG/engine/server/sv_cof_retail.h`, after the `SV_CoF_RetailFreeSlots` declaration, add:

```c
// The game's three-slot mode (cof_clearitems sf 4): only slots 1..3 usable. False without a verified player.
qboolean SV_CoF_RetailInThreeSlots( sv_client_t *cl );
```

In `sv_cof_retail.c`, after `SV_CoF_RetailFreeSlots`, add:

```c
qboolean SV_CoF_RetailInThreeSlots( sv_client_t *cl )
{
	byte *pv = CoF_RetailPlayer( cl );
	return pv && pv[COF_RETAIL_THREE_SLOTS];
}
```

- [ ] **Step 6: Run the test.** `python -m unittest tests.test_enhanced_inventory12 -v`. Expected: `test_rules ... ok`, `test_sites_against_retail_hl ... ok`. Also `python -m unittest tests.test_enhanced_retail -v` (the retail harness compiles all of `sv_cof_retail.c`). Expected: OK.

- [ ] **Step 7: Wire it in.**
  - `ENG/engine/server/sv_init.c`: add `#include "sv_cof_inventory12.h"` beside the other `sv_cof_*` includes, and after `SV_CoF_StateActivate();` (line ~599) add `SV_CoF_Inventory12Activate();`.
  - `ENG/engine/server/sv_cof_story.c`: add `#include "sv_cof_inventory12.h"` beside the other includes; make `SV_CoF_Inventory12Close();` the first statement of `CoF_StoryCloseGate`; add `SV_CoF_Inventory12Init();` at the end of `SV_CoF_StoryInit`; add `SV_CoF_Inventory12Frame();` in `SV_CoF_StoryFrame` next to the other per-frame module calls (after its early returns).
  - `ENG/engine/server/sv_client.c`: add `#include "sv_cof_inventory12.h"` beside the other `sv_cof_*` includes. In `SV_ConnectClient`, directly after the connection-password block (~line 416, before the slot search), add:

```c
	// Cry of Fear story co-op: a map at 12 inventory slots needs the 12-slot screen
	if( SV_CoF_Inventory12Refuse( userinfo, NET_IsLocalAddress( from )))
	{
		SV_RejectConnection( from, "This game uses the 12-slot inventory.\nUpdate Cry of Fear story co-op to join.\n" );
		return;
	}
```

  In `SV_ExecuteClientCommand`, after `if( SV_CoF_SceneClientCommand( cl )) return;`, add:

```c
	// Cry of Fear story co-op: inventory commands only name slots this map has
	if( SV_CoF_Inventory12ClientCommand( cl ))
		return;
```

  - `ENG/engine/common/common.h`: next to `unsigned int CL_CoF_PanelsOnScreen( qboolean *reporting );` (~line 779) add `qboolean CL_CoF_Inventory12Ready( void ); // cof_inventory12.c: client.dll's slot accessor is the engine's and the UI library has the 12-slot grid`.
  - `ENG/engine/common/dedicated.c`: inside its `#if XASH_DEDICATED` block, with the other stubs, add:

```c
qboolean CL_CoF_Inventory12Ready( void )
{
	return false; // no local client
}
```

- [ ] **Step 8: Run the server tests.** `python -m unittest tests.test_enhanced_inventory12 tests.test_enhanced_retail tests.test_enhanced_party tests.test_enhanced_userinfo -v`. Expected: all OK (`test_enhanced_party` slices `sv_cof_story.c`; if it now needs a double for `SV_CoF_Inventory12Close`/`Init`/`Frame`, add empty doubles to `tests/engine/cof_party_harness.c` the way it stubs the other modules).

- [ ] **Step 9: Commit** (see Global Constraints). Engine: the new files and the five modified engine files, message "Story co-op: the 12-slot inventory's server rules". Launcher: the harness and the test file, message "Test the 12-slot inventory's server rules".

---

### Task 3: The engine's own inventory code carries 12 slots

**Files:**
- Modify: `ENG/engine/server/sv_cof_retail.h:6`, `sv_cof_retail.c:12-14, 193-206`, `sv_cof_share_state.h:14`, `sv_cof_party.c:2-4, 23-25`, `sv_cof_story.c:215-226`
- Modify: `ROOT/tests/engine/cof_retail_harness.c`, `cof_share_harness.c`, `cof_moments_harness.c`, `cof_party_harness.c`, `ROOT/tests/test_story_fixture.py`, `ROOT/tools/verify_story_fixture.py`

**Interfaces:**
- Consumes (Task 2): `int SV_CoF_InventorySlots( void )`.
- Produces: `COF_RETAIL_SLOTS` = `COF_SHARE_SLOTS` = 12 (the most the engine reads and carries); `SV_CoF_RetailReadSlots` fills 12 entries; `SV_CoF_RetailFreeSlots` counts 1..3 (three-slot mode), else 1..`SV_CoF_InventorySlots()`; `cof_story_status` prints `inventory-slot ... index=0..12`.
- Rule (spec section 1): data paths always carry 12; rules that pick or count slots use the active count.

- [ ] **Step 1: Failing tests.**
  - `tests/engine/cof_retail_harness.c`: before `#include "retail_under_test.c"` add `static int inventory_slots = 6; static int SV_CoF_InventorySlots( void ) { return inventory_slots; }`. After the existing three-slot assertions (the block ending `private_data[0][0x1E9C] = 0;`) add:

```c
	/* twelve slots: the engine reads all of them; free slots count to the map's size */
	inventory_slots = 12;
	id = 3; memcpy( private_data[0] + 0x21FC + 12 * sizeof( id ), &id, sizeof( id ));
	assert( SV_CoF_RetailReadSlots( &clients[0], slots ) && !strcmp( slots[11], "inventoryitems/fuse.txt" ) && !slots[6][0] );
	assert( SV_CoF_RetailFreeSlots( &clients[0] ) == 9 );
	private_data[0][0x1E9C] = 1;
	assert( SV_CoF_RetailFreeSlots( &clients[0] ) == 2 && SV_CoF_RetailInThreeSlots( &clients[0] ));
	private_data[0][0x1E9C] = 0;
	inventory_slots = 6; /* a map at six: slot 12 is kept but not counted */
	assert( SV_CoF_RetailFreeSlots( &clients[0] ) == 4 && !SV_CoF_RetailInThreeSlots( &clients[0] ));
	id = 0; memcpy( private_data[0] + 0x21FC + 12 * sizeof( id ), &id, sizeof( id ));
```

  - `tests/engine/cof_share_harness.c`: change `#define COF_RETAIL_SLOTS 6` to `12`; add `static int active = 6; /* the map's slot count (SV_CoF_InventorySlots) */` after the `three[2]` line; in `SV_CoF_RetailFreeSlots` and the add loop of `SV_CoF_RetailGiveItem` replace the literal `6` of the usable count with `active`; replace the literal `6` bound in the "never a second copy" loop, `SV_CoF_RetailRemoveItem`, `holds`, `lose` and `gain` with `COF_RETAIL_SLOTS`. Then append a scenario before the final `puts` (use the harness's own helpers for setting up two players and running `frame`, as the scenarios above it do):

```c
	/* twelve slots: a partner whose slots 1..6 are full still gets the shared key, in slot 7 */
	active = 12;
	/* reuse the setup of the scenario at line ~158 ("inventory full"): player 1 holds six Glocks */
	for( i = 0; i < 6; i++ ) strcpy( inv[1][i], GLOCK );
	gain( 0, KEY ); frame( 0.1 ); frame( 1.0 );
	assert( !strcmp( inv[1][6], KEY ));
	active = 6;
```

   (`KEY` is the story-item descriptor the harness's other scenarios share; if the harness names it differently, use that name. The point: at 12 the full-at-six partner is no longer "full".)
  - `tests/engine/cof_moments_harness.c`: change `#define COF_RETAIL_SLOTS 6` to `12`; in its `SV_CoF_RetailReadSlots` double put the phone in `slots[has_phone[cl - clients] == 2 ? 9 : 2]`; before the final `puts` add:

```c
	/* a phone in slot 10 rings like one in slot 3 */
	has_phone[0] = 2; assert( CoF_MomentHasPhone( &clients[0] ));
	has_phone[0] = 0; assert( !CoF_MomentHasPhone( &clients[0] ));
```

  - `tests/engine/cof_party_harness.c`: after `assert(strstr(status_log,"index=3 descriptor=\"inventoryitems/weapons/weapon_glock.txt\""));` add `assert(strstr(status_log,"index=12 descriptor=\"\"") && !strstr(status_log,"index=13 "));`.
  - `tests/test_story_fixture.py`: in `status_capture()` add the six lines `cofstory phase=inventory-slot slot=1 userid=101 index=7 descriptor=""` … `index=12 descriptor=""` after the `index=6` line; in the parse test add, next to the `index=6` removal case:

```python
        with self.assertRaises(ValueError):
            parse_status("\n".join(line for line in text.splitlines() if "index=12" not in line), "initial")
        with self.assertRaises(ValueError):
            parse_status(text.replace('index=7 descriptor=""', 'index=13 descriptor=""'), "initial")
```

   and a case proving an older seven-slot capture still parses:

```python
    def test_seven_slot_capture_still_parses(self):
        text = "\n".join(line for line in status_capture().splitlines()
                         if not any(f"index={i} " in line for i in range(7, 13)))
        self.assertEqual(parse_status(text, "initial")["players"][0]["inventory"],
                         parse_status(status_capture(), "initial")["players"][0]["inventory"])
```

   (Place it in the same `TestCase` class as the other `parse_status` tests; adapt the trailing `cofstory phase=status-end` handling if `status_capture()` already returns it.)

- [ ] **Step 2: Run them to see them fail.** `python -m unittest tests.test_enhanced_retail tests.test_enhanced_share tests.test_enhanced_moments tests.test_enhanced_party tests.test_story_fixture -v`. Expected: failures (`slots[11]` out of bounds or wrong, `index=12` missing, the `#error` in `sv_cof_share.c` because `COF_SHARE_SLOTS` is still 6, the verifier rejecting index 7).

- [ ] **Step 3: Change the engine.**
  - `sv_cof_retail.h:6`: `#define COF_RETAIL_SLOTS 12 // inventory slots 1..12 the engine reads and carries (index 0 is never used); SV_CoF_InventorySlots() says how many this map uses`. Update the comment above `SV_CoF_RetailReadSlots` to "usable slots 1..12".
  - `sv_cof_retail.c`: add `#include "sv_cof_inventory12.h"`; line 12 comment: `// string_t inventorySlot[], slots 1..12 in place (AddInventoryItem 100DAD40; sv_cof_inventory12_patch.h)`; line 14: `#define COF_RETAIL_PLAYER_NEED ( COF_RETAIL_INVENTORY + ( COF_RETAIL_SLOTS + 1 ) * sizeof( string_t ))`; in `SV_CoF_RetailFreeSlots`: `usable = pv[COF_RETAIL_THREE_SLOTS] ? 3 : SV_CoF_InventorySlots();`.
  - `sv_cof_share_state.h:14`: `#define COF_SHARE_SLOTS 12 // = COF_RETAIL_SLOTS (sv_cof_share.c checks)`.
  - `sv_cof_party.c:2-4`: the comment becomes "Retail 1.6 player: inventorySlot[] at +21FC holds item descriptor paths, slots 1..12 in story co-op (sv_cof_inventory12_patch.h); slot 0 is unused." Lines 24-25: `#define COF_PARTY_SLOTS 13 // indices 0..12; 0 is never used`, `COF_PARTY_PLAYER_SIZE` unchanged in form (it now evaluates to 0x2230).
  - `sv_cof_story.c` status dump (~line 219): `for( j = 0; j <= 12; j++ ) // inventorySlot[0..12]; 1..12 hold items in story co-op (sv_cof_inventory12_patch.h)` (a literal: `tests/engine/cof_party_harness.c` compiles this function without the retail header; the read already asks for 0x2250, which covers 0x222F).
  - `sv_cof_moments.c`, `sv_cof_share.c`, `sv_cof_testgun.c`, `sv_cof_companion.c`: no change (they loop `COF_RETAIL_SLOTS` or call `SV_CoF_RetailFreeSlots`).
- [ ] **Step 4: The verifier.** `tools/verify_story_fixture.py:89`: `if index not in range(13) or index in observed:`; line ~112: `if set(observed) not in (set(range(7)), set(range(13))) or any(slot != player["slot"] for slot in observed.values()):` with a comment `# 13 since the 12-slot inventory (2026-10-14); 7 in older captures`.
- [ ] **Step 5: Run them to see them pass.** Same command as Step 2. Expected: all OK. Then the whole engine group: `python -m unittest tests.test_enhanced_state tests.test_enhanced_recovery tests.test_enhanced_session tests.test_enhanced_share_state tests.test_enhanced_testgun tests.test_enhanced_companion -v`. Expected: OK (fix any harness that hard-codes six slots the same way: buffers sized by the macro, the active count where it models the game's free-slot rule).
- [ ] **Step 6: Commit** (see Global Constraints). Engine: "Story co-op: the engine's inventory code carries 12 slots". Launcher: "Tests and the fixture verifier for 12 inventory slots".

---

### Task 4: The capability key fits a full userinfo; the SHA-256 code is shared

**Files:**
- Modify: `ENG/engine/common/infostring.c` (`Info_IsKeyImportant`, ~line 330), `ENG/engine/server/sv_cof_cheats.c` (~lines 279-320), `ENG/engine/common/common.h`
- Modify: `ROOT/tests/engine/cof_userinfo_harness.c`

**Interfaces:**
- Produces: `qboolean COM_CoF_ModuleSha256Is( const void *module, const char *hex )` (declared in `common.h`; `module` is a `dll_user_t *`; true when the file the module was loaded from has that SHA-256, lowercase or uppercase hex; false where the hashing code is not built). `_cofinv` is an important userinfo key.

- [ ] **Step 1: Failing test.** In `tests/engine/cof_userinfo_harness.c`, before the final `puts(...)`, add:

```c
    /* The 12-slot inventory's capability key must survive the same full retail
       userinfo, or a 12-slot host turns the player away. */
    CHECK(Info_SetValueForKey(g.info,"_cofinv","12",sizeof(g.info)));
    CHECK(!strcmp(Info_ValueForKey(g.info,"_cofinv"),"12"));
    CHECK(!strcmp(Info_ValueForKey(g.info,"password"),"0123456789abcdef"));
    CHECK(!strcmp(Info_ValueForKey(g.info,"name"),"Little Rascal 2"));
    CHECK(strlen(g.info)<sizeof(g.info));CHECK(guarded(&g));
```

- [ ] **Step 2: Run it to see it fail.** `python -m unittest tests.test_enhanced_userinfo -v`. Expected: FAIL (`_cofinv` is silently dropped at 248+ characters).

- [ ] **Step 3: The important key.** In `ENG/engine/common/infostring.c`, `Info_IsKeyImportant`, after the `password` case add:

```c
	// The story co-op 12-slot inventory's capability (cof_inventory12.c): without
	// it a host running 12 slots turns the player away.
	if( !Q_strcmp( key, "_cofinv" ))
		return true;
```

- [ ] **Step 4: Run it to see it pass.** `python -m unittest tests.test_enhanced_userinfo -v`. Expected: OK.

- [ ] **Step 5: Share the hash.** In `ENG/engine/server/sv_cof_cheats.c`, split `CoF_HashModuleFile` so the hashing returns the hex digest and the hl.dll comparison stays where it is:

```c
#if COF_CHEATS_SUPPORTED
// SHA-256 of the file a loaded module came from; false when it cannot be read.
static qboolean CoF_HashModuleHex( const dll_user_t *hInst, char hex[65] )
{
	wchar_t path[2048];
	cof_sha256_t ctx;
	byte buf[16384];
	size_t n;
	FILE *f;

	path[0] = 0;

	// the file the loader actually mapped; the loader's own resolved path is
	// the fallback (and the only answer for the in-memory loader)
	if( !hInst->custom_loader && hInst->hInstance )
	{
		DWORD len = GetModuleFileNameW((HMODULE)hInst->hInstance, path, ARRAYSIZE( path ));

		if( len == 0 || len >= ARRAYSIZE( path ))
			path[0] = 0;
	}

	if( !path[0] && !MultiByteToWideChar( CP_UTF8, 0, hInst->fullPath, -1, path, ARRAYSIZE( path )))
		return false;

	f = _wfopen( path, L"rb" );
	if( !f )
		return false;

	CoF_Sha256Init( &ctx );
	while(( n = fread( buf, 1, sizeof( buf ), f )) > 0 )
		CoF_Sha256Update( &ctx, buf, n );

	if( ferror( f ))
	{
		fclose( f );
		return false;
	}

	fclose( f );
	CoF_Sha256Final( &ctx, hex );
	return true;
}

static cof_hash_state_t CoF_HashModuleFile( const dll_user_t *hInst, char hex[65] )
{
	if( !CoF_HashModuleHex( hInst, hex ))
		return COF_HASH_UNREADABLE;
	return !Q_strcmp( hex, cof_retail_hl_sha256 ) ? COF_HASH_MATCH : COF_HASH_MISMATCH;
}
#endif // COF_CHEATS_SUPPORTED

// The client checks client.dll with the same code (cof_inventory12.c).
qboolean COM_CoF_ModuleSha256Is( const void *module, const char *hex )
{
#if COF_CHEATS_SUPPORTED
	char got[65];
	return module && hex && CoF_HashModuleHex( (const dll_user_t *)module, got ) && !Q_stricmp( got, hex );
#else
	return false;
#endif
}
```

  In `ENG/engine/common/common.h`, next to the `CL_CoF_Inventory12Ready` declaration from Task 2, add `qboolean COM_CoF_ModuleSha256Is( const void *module, const char *hex ); // sv_cof_cheats.c: the SHA-256 of the file a dll_user_t was loaded from`.

- [ ] **Step 6: Run the cheats-related tests.** `python -m unittest tests.test_enhanced_userinfo tests.test_enhanced_retail -v` and any test that compiles `sv_cof_cheats.c` (`grep -l sv_cof_cheats tests/*.py`). Expected: OK.
- [ ] **Step 7: Commit** (see Global Constraints). Engine: "Story co-op: _cofinv survives a full userinfo; share the module SHA-256". Launcher: "Test _cofinv in the full retail userinfo".

---

### Task 5: Client slot store, message routing and the accessor hook

**Files:**
- Create: `ENG/engine/client/cof_inventory12_state.h`, `ENG/engine/client/cof_inventory12_patch.h`
- Create: `ROOT/tests/engine/cof_inv12_client_harness.c`, `ROOT/tests/engine/cof_inv12_accessor_harness.c`
- Modify: `ROOT/tests/test_enhanced_inventory12.py`

**Interfaces:**
- Produces (`cof_inventory12_state.h`, pure C): `COF_INV12_SLOTS` (12), `COF_INV12_TEXT` (128), `cof_inv12_slots_t { char slot[13][128]; char scratch[128]; int three; }`, `cof_inv12_message_t { int slot, reset, three, silent, tac; char descriptor[128]; }`, `COF_INV12_FORWARD/KEPT/DROPPED`, `int CoF_Inv12Parse( const unsigned char *buf, int size, cof_inv12_message_t *m )`, `int CoF_Inv12Route( cof_inv12_slots_t *s, const cof_inv12_message_t *m )`, `char *CoF_Inv12Slot( cof_inv12_slots_t *s, int n )`, `void CoF_Inv12Tac( cof_inv12_slots_t *s, int tac )`, `void CoF_Inv12ResetHigh( cof_inv12_slots_t *s )`, `int CoF_Inv12PickupLine( const char *nice, unsigned char *out, int size )`.
- Produces (`cof_inventory12_patch.h`, Win32 x86): `COF_INV12_ACCESSOR` (0x6CDD0), `cof_inv12_accessor_stock[15]`, `cof_inv12_hook_t { unsigned char *site; unsigned char saved[5]; }`, `int CoF_Inv12HookAccessor( cof_inv12_hook_t *h, unsigned char *base, const void *target )`, `void CoF_Inv12UnhookAccessor( cof_inv12_hook_t *h )`. The target is a `char * __stdcall f( int slot )`.

- [ ] **Step 1: Failing tests.** Create `ROOT/tests/engine/cof_inv12_client_harness.c`:

```c
/* The client's 12-slot store (cof_inventory12_state.h): which Inventory messages
 * reach client.dll, what the engine keeps, the tac-glock rename and the pickup line. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "state_under_test.h"

/* byte slot, byte reset, string descriptor, byte three, byte silent, byte tac (hl.dll 100E4360) */
static int Message( unsigned char *b, int slot, const char *d, int three, int silent, int tac )
{
	int n = 0;
	b[n++] = (unsigned char)slot; b[n++] = 0;
	strcpy( (char *)b + n, d ); n += (int)strlen( d ) + 1;
	b[n++] = (unsigned char)three; b[n++] = (unsigned char)silent; b[n++] = (unsigned char)tac;
	return n;
}

int main( void )
{
	static cof_inv12_slots_t s;
	cof_inv12_message_t m;
	unsigned char b[400], line[200];
	char long_name[200];
	int n;

	n = Message( b, 3, "inventoryitems/apartmentkey.txt", 0, 0, 0 );
	assert( CoF_Inv12Parse( b, n, &m ) && m.slot == 3 && !strcmp( m.descriptor, "inventoryitems/apartmentkey.txt" ));
	assert( CoF_Inv12Route( &s, &m ) == COF_INV12_FORWARD && !strcmp( s.slot[3], m.descriptor ));

	/* 7..12 are kept from client.dll, which would write them into read-only memory */
	n = Message( b, 9, "inventoryitems/weapons/weapon_glock.txt", 0, 1, 0 );
	assert( CoF_Inv12Parse( b, n, &m ) && m.silent == 1 );
	assert( CoF_Inv12Route( &s, &m ) == COF_INV12_KEPT && !strcmp( s.slot[9], "inventoryitems/weapons/weapon_glock.txt" ));
	n = Message( b, 12, "", 0, 0, 0 );
	assert( CoF_Inv12Parse( b, n, &m ) && CoF_Inv12Route( &s, &m ) == COF_INV12_KEPT && !s.slot[12][0] );

	/* slot 0 and 13+: dropped, nothing stored */
	n = Message( b, 0, "inventoryitems/x.txt", 0, 0, 0 );
	assert( CoF_Inv12Parse( b, n, &m ) && CoF_Inv12Route( &s, &m ) == COF_INV12_DROPPED );
	n = Message( b, 13, "inventoryitems/x.txt", 0, 0, 0 );
	assert( CoF_Inv12Parse( b, n, &m ) && CoF_Inv12Route( &s, &m ) == COF_INV12_DROPPED );

	/* malformed: too short, or no terminator, or the trailing bytes missing */
	assert( !CoF_Inv12Parse( b, 5, &m ));
	memset( b, 'a', 40 ); b[0] = 3; b[1] = 0;
	assert( !CoF_Inv12Parse( b, 40, &m ));
	n = Message( b, 3, "inventoryitems/x.txt", 0, 0, 0 );
	assert( !CoF_Inv12Parse( b, n - 1, &m ));

	/* a descriptor longer than 127 characters is cut like client.dll's strncpy */
	memset( long_name, 'q', 150 ); long_name[150] = 0;
	n = Message( b, 8, long_name, 0, 0, 0 );
	assert( CoF_Inv12Parse( b, n, &m ) && strlen( m.descriptor ) == 127 );

	/* the tac glock: client.dll renames slots 1..6, the engine 7..12, both ways */
	n = Message( b, 4, "", 0, 0, 1 );
	assert( CoF_Inv12Parse( b, n, &m ) && CoF_Inv12Route( &s, &m ) == COF_INV12_FORWARD );
	assert( !strcmp( s.slot[9], "inventoryitems/weapons/weapon_glocktac.txt" ));
	n = Message( b, 4, "", 0, 0, 0 );
	assert( CoF_Inv12Parse( b, n, &m ) && CoF_Inv12Route( &s, &m ) == COF_INV12_FORWARD );
	assert( !strcmp( s.slot[9], "inventoryitems/weapons/weapon_glock.txt" ));

	/* the three-slot flag of the last message */
	n = Message( b, 1, "", 1, 0, 0 );
	assert( CoF_Inv12Parse( b, n, &m ) && CoF_Inv12Route( &s, &m ) == COF_INV12_FORWARD && s.three == 1 );

	/* ResetHUD: client.dll clears 1..6 itself, the engine 7..12 */
	CoF_Inv12ResetHigh( &s );
	assert( !s.slot[9][0] && !strcmp( s.slot[3], "inventoryitems/apartmentkey.txt" ));

	/* the accessor: 1..12 are the store, anything else a writable scratch buffer */
	assert( CoF_Inv12Slot( &s, 1 ) == s.slot[1] && CoF_Inv12Slot( &s, 12 ) == s.slot[12] );
	assert( CoF_Inv12Slot( &s, 13 ) == s.scratch && CoF_Inv12Slot( &s, 0 ) == s.scratch && CoF_Inv12Slot( &s, -5 ) == s.scratch );

	/* the pickup line, as client.dll writes it (ProFont: colour 5, text) */
	n = CoF_Inv12PickupLine( "Glock 19 9mm", line, sizeof( line ));
	assert( n == (int)strlen( "You got the Glock 19 9mm" ) + 2 && line[0] == 5 && !strcmp( (char *)line + 1, "You got the Glock 19 9mm" ));
	n = CoF_Inv12PickupLine( "Simon's Book", line, sizeof( line ));
	assert( n > 0 && !strcmp( (char *)line + 1, "You got Simon's Book" ));
	assert( !CoF_Inv12PickupLine( "", line, sizeof( line )));
	assert( !CoF_Inv12PickupLine( "Glock", line, 10 ));
	puts( "inventory12 client store passed" );
	return 0;
}
```

Create `ROOT/tests/engine/cof_inv12_accessor_harness.c`:

```c
/* client.dll's slot accessor (1006CDD0) hooked to the engine's store, against
 * the real retail client.dll, mapped (never initialised) by the Windows loader
 * at a base other than its preferred one, as ASLR may load it. */
#include <windows.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "state_under_test.h"
#include "patch_under_test.h"

static cof_inv12_slots_t store;
static char * __stdcall Accessor( int slot ) { return CoF_Inv12Slot( &store, slot ); }
/* __thiscall: self in ecx, the slot on the stack, ret 4 - the same as __fastcall( self, edx, slot ) */
typedef char *( __fastcall *accessor_t )( void *self, void *unused, int slot );

static void Poke( unsigned char *p, unsigned char value )
{
	DWORD protect, ignored;
	assert( VirtualProtect( p, 1, PAGE_EXECUTE_READWRITE, &protect ));
	*p = value;
	VirtualProtect( p, 1, protect, &ignored );
}

int main( int argc, char **argv )
{
	static char hud[0x3200]; /* stands in for gHUD: the stock accessor returns hud + 0x2C64 + ( n - 1 ) * 0x80 */
	cof_inv12_hook_t hook;
	unsigned char *base;
	accessor_t call;
	void *taken;
	assert( argc == 2 );
	memset( &hook, 0, sizeof( hook ));
	taken = VirtualAlloc( (void *)0x10000000, 0x1000, MEM_RESERVE, PAGE_NOACCESS ); /* push the DLL off its preferred base */
	base = (unsigned char *)LoadLibraryExA( argv[1], NULL, DONT_RESOLVE_DLL_REFERENCES );
	assert( base );
	assert( !taken || base != (unsigned char *)0x10000000 );
	call = (accessor_t)( base + COF_INV12_ACCESSOR );

	/* stock */
	assert( call( hud, NULL, 1 ) == hud + 0x2C64 && call( hud, NULL, 6 ) == hud + 0x2C64 + 5 * 0x80 );

	/* hooked: 1..12 are the store, anything else the scratch buffer */
	assert( CoF_Inv12HookAccessor( &hook, base, (const void *)Accessor ));
	assert( !CoF_Inv12HookAccessor( &hook, base, (const void *)Accessor )); /* once */
	assert( call( hud, NULL, 1 ) == store.slot[1] && call( hud, NULL, 7 ) == store.slot[7] && call( hud, NULL, 12 ) == store.slot[12] );
	assert( call( hud, NULL, 13 ) == store.scratch && call( hud, NULL, 0 ) == store.scratch );
	strcpy( call( hud, NULL, 13 ), "x" ); /* the stock fallback was read-only: this crashed client.dll */

	/* unhooked: the stock bytes and behaviour are back */
	CoF_Inv12UnhookAccessor( &hook );
	assert( !hook.site && !memcmp( base + COF_INV12_ACCESSOR, cof_inv12_accessor_stock, sizeof( cof_inv12_accessor_stock )));
	assert( call( hud, NULL, 2 ) == hud + 0x2C64 + 0x80 );

	/* unexpected bytes: refuse, write nothing */
	Poke( base + COF_INV12_ACCESSOR + 9, 0x0b );
	assert( !CoF_Inv12HookAccessor( &hook, base, (const void *)Accessor ) && base[COF_INV12_ACCESSOR] == 0x55 );
	Poke( base + COF_INV12_ACCESSOR + 9, 0x05 );
	FreeLibrary( (HMODULE)base );
	puts( "inventory12 client.dll accessor passed" );
	return 0;
}
```

Add to `ROOT/tests/test_enhanced_inventory12.py`:

```python
class ClientStoreTests(unittest.TestCase):
    def test_store(self):
        out = run_native(self, 'cof_inv12_client_harness.c',
                         {'state_under_test.h': (ENGINE_CLIENT / 'cof_inventory12_state.h').read_text()})
        self.assertIn('inventory12 client store passed', out)

    def test_accessor_against_retail_client(self):
        if not retail(CLIENT_DLL, CLIENT_SHA):
            self.skipTest('the retail Cry of Fear 1.6 client.dll is not installed locally')
        out = run_x86(self, 'cof_inv12_accessor_harness.c', {
            'state_under_test.h': (ENGINE_CLIENT / 'cof_inventory12_state.h').read_text(),
            'patch_under_test.h': (ENGINE_CLIENT / 'cof_inventory12_patch.h').read_text(),
        }, [CLIENT_DLL])
        self.assertIn('inventory12 client.dll accessor passed', out)
```

- [ ] **Step 2: Run them to see them fail.** `python -m unittest tests.test_enhanced_inventory12 -v`. Expected: the two new tests FAIL (`FileNotFoundError`).

- [ ] **Step 3: The store.** Create `ENG/engine/client/cof_inventory12_state.h`:

```c
/* cof_inventory12_state.h - Cry of Fear story co-op: the client's copy of the
 * 12 inventory slots. GPL-3.0-or-later.
 *
 * Pure: no engine types; included by cof_inventory12.c and by the offline
 * harness. client.dll keeps char[128] per slot for slots 1..6 only, reached
 * through one accessor (1006CDD0), which the engine redirects here
 * (cof_inventory12_patch.h). Its Inventory handler copies the descriptor
 * before any range check, into read-only memory for slot 0 or 7+: those never
 * reach it. evidence/inventory-grid-20261013/client-inventory-re.md section 2.
 */
#ifndef COF_INVENTORY12_STATE_H
#define COF_INVENTORY12_STATE_H
#include <stdio.h>
#include <string.h>

#define COF_INV12_SLOTS 12
#define COF_INV12_TEXT 128

typedef struct
{
	char slot[COF_INV12_SLOTS + 1][COF_INV12_TEXT]; // 1..12; [0] is never used
	char scratch[COF_INV12_TEXT];                   // the accessor's answer for any other number: writable, never read
	int three;                                      // the story's three-slot sections (from the last Inventory message)
} cof_inv12_slots_t;

typedef struct
{
	int slot, reset, three, silent, tac;
	char descriptor[COF_INV12_TEXT];
} cof_inv12_message_t;

enum { COF_INV12_FORWARD = 0, COF_INV12_KEPT, COF_INV12_DROPPED };

/* Inventory (hl.dll 100E4360): byte slot, byte reset, string descriptor, byte
 * three, byte silent, byte tac glock. 0 when malformed. */
static int CoF_Inv12Parse( const unsigned char *buf, int size, cof_inv12_message_t *m )
{
	int i = 2, n = 0;
	memset( m, 0, sizeof( *m ));
	if( size < 6 ) return 0;
	m->slot = buf[0];
	m->reset = buf[1];
	for( ; i < size && buf[i]; i++ )
		if( n + 1 < COF_INV12_TEXT ) m->descriptor[n++] = (char)buf[i];
	if( i + 3 >= size ) return 0; // the terminator and three bytes after it
	m->three = buf[i + 1];
	m->silent = buf[i + 2];
	m->tac = buf[i + 3];
	return 1;
}

/* What client.dll's accessor returns for slot n. */
static char *CoF_Inv12Slot( cof_inv12_slots_t *s, int n )
{
	if( n >= 1 && n <= COF_INV12_SLOTS ) return s->slot[n];
	memset( s->scratch, 0, sizeof( s->scratch ));
	return s->scratch;
}

/* client.dll's tac-glock rename (it does slots 1..6 and the quick slots), for 7..12. */
static void CoF_Inv12Tac( cof_inv12_slots_t *s, int tac )
{
	const char *from = tac ? "weapon_glock.txt" : "weapon_glocktac.txt";
	const char *to = tac ? "weapon_glocktac.txt" : "weapon_glock.txt";
	size_t fl = strlen( from ), tl = strlen( to );
	int n;
	for( n = 7; n <= COF_INV12_SLOTS; n++ )
	{
		char *d = s->slot[n];
		size_t len = strlen( d );
		if( len < fl || strcmp( d + len - fl, from ) || len - fl + tl >= COF_INV12_TEXT ) continue;
		memcpy( d + len - fl, to, tl + 1 );
	}
}

/* 1..6 go on to client.dll, which writes them through the accessor; 7..12 are
 * kept here; anything else is dropped. The store follows every one it takes. */
static int CoF_Inv12Route( cof_inv12_slots_t *s, const cof_inv12_message_t *m )
{
	if( m->slot < 1 || m->slot > COF_INV12_SLOTS ) return COF_INV12_DROPPED;
	s->three = m->three;
	memcpy( s->slot[m->slot], m->descriptor, sizeof( m->descriptor ));
	CoF_Inv12Tac( s, m->tac );
	return m->slot <= 6 ? COF_INV12_FORWARD : COF_INV12_KEPT;
}

/* ResetHUD: client.dll clears 1..6 itself (through the accessor). */
static void CoF_Inv12ResetHigh( cof_inv12_slots_t *s )
{
	int n;
	for( n = 7; n <= COF_INV12_SLOTS; n++ ) s->slot[n][0] = 0;
}

/* The line client.dll shows for a pickup, as a ProFont payload: colour 5, then
 * "You got the X" ("You got Simon's Book"). Its length, or 0. */
static int CoF_Inv12PickupLine( const char *nice, unsigned char *out, int size )
{
	const char *format = !strcmp( nice, "Simon's Book" ) ? "You got %s" : "You got the %s";
	int n;
	if( !nice[0] || size < 3 ) return 0;
	out[0] = 5;
	n = snprintf( (char *)out + 1, (size_t)size - 1, format, nice );
	if( n < 0 || n >= size - 1 ) return 0;
	return n + 2;
}

#endif /* COF_INVENTORY12_STATE_H */
```

- [ ] **Step 4: The hook.** Create `ENG/engine/client/cof_inventory12_patch.h`:

```c
/* cof_inventory12_patch.h - Cry of Fear story co-op: client.dll's slot-string
 * accessor redirected to the engine's 12-slot store. GPL-3.0-or-later.
 *
 * Win32 x86 only; included by cof_inventory12.c and by the offline harness,
 * which runs it against the real DLL. client.dll D2A04641...9C00, accessor at
 * RVA 0x6CDD0: char *__thiscall( int slot ), ret 4. The DLL is ASLR-relocatable
 * and only the accessor's first 15 bytes carry no relocation, so those are the
 * ones compared; the jump is computed from the real address. */
#ifndef COF_INVENTORY12_PATCH_H
#define COF_INVENTORY12_PATCH_H
#include <windows.h>
#include <stdint.h>
#include <string.h>

#define COF_INV12_ACCESSOR 0x6CDD0

/* push ebp; mov ebp,esp; mov eax,[ebp+8]; dec eax; cmp eax,5; ja +43h; jmp [eax*4+table] (opcode bytes) */
static const unsigned char cof_inv12_accessor_stock[15] = { 0x55, 0x8b, 0xec, 0x8b, 0x45, 0x08, 0x48, 0x83, 0xf8, 0x05, 0x77, 0x43, 0xff, 0x24, 0x85 };

typedef struct
{
	unsigned char *site;    // the hooked accessor, NULL when not hooked
	unsigned char saved[5]; // its first five bytes
} cof_inv12_hook_t;

/* A jmp rel32 to target, a char *__stdcall( int slot ): the jump keeps the
 * caller's return address and argument, so the target's ret 4 returns straight
 * to client.dll; ecx (the HUD) is not needed. 1 when written. */
static int CoF_Inv12HookAccessor( cof_inv12_hook_t *h, unsigned char *base, const void *target )
{
	unsigned char *p = base + COF_INV12_ACCESSOR, code[5];
	DWORD protect, ignored;
	int32_t rel;
	if( h->site || memcmp( p, cof_inv12_accessor_stock, sizeof( cof_inv12_accessor_stock ))) return 0;
	rel = (int32_t)((const unsigned char *)target - ( p + 5 ));
	code[0] = 0xe9;
	memcpy( code + 1, &rel, sizeof( rel ));
	if( !VirtualProtect( p, sizeof( code ), PAGE_EXECUTE_READWRITE, &protect )) return 0;
	memcpy( h->saved, p, sizeof( code ));
	memcpy( p, code, sizeof( code ));
	FlushInstructionCache( GetCurrentProcess(), p, sizeof( code ));
	VirtualProtect( p, sizeof( code ), protect, &ignored );
	h->site = p;
	return 1;
}

static void CoF_Inv12UnhookAccessor( cof_inv12_hook_t *h )
{
	DWORD protect, ignored;
	if( !h->site ) return;
	if( VirtualProtect( h->site, sizeof( h->saved ), PAGE_EXECUTE_READWRITE, &protect ))
	{
		memcpy( h->site, h->saved, sizeof( h->saved ));
		FlushInstructionCache( GetCurrentProcess(), h->site, sizeof( h->saved ));
		VirtualProtect( h->site, sizeof( h->saved ), protect, &ignored );
	}
	h->site = NULL;
}

#endif /* COF_INVENTORY12_PATCH_H */
```

- [ ] **Step 5: Run them to see them pass.** `python -m unittest tests.test_enhanced_inventory12 -v`. Expected: all four tests OK.
- [ ] **Step 6: Commit** (see Global Constraints). Engine: "Story co-op: the client's 12-slot store and client.dll accessor hook". Launcher: "Test the client's 12-slot store and the accessor hook against client.dll".

---

### Task 6: Client glue and the `CofInventory` interface

**Files:**
- Create: `ENG/engine/client/cof_inventory12.c`
- Modify: `ENG/engine/vgui_api.h` (types before `vguiapi_t`, a member after `CofPanelAction`), `ENG/engine/client/vgui/vgui_draw.c` (~lines 438-489), `ENG/engine/client/parse/cl_parse.c` (~line 2296), `ENG/engine/client/dll_int/cl_game.c` (`CL_LoadProgs` ~line 4733, `CL_UnloadProgs` ~line 4544), `ENG/engine/client/cl_main.c` (`CL_ClearState` ~line 3933, `CL_InitLocal` ~line 6189)

**Interfaces:**
- Consumes: Task 4 `COM_CoF_ModuleSha256Is`; Task 5 headers; existing `CL_DispatchUserMessage( const char *name, int size, void *buf )`, `CL_ServerCommand`, `S_StartLocalSound( const char *name, float volume, qboolean reliable )`, `FS_LoadImage`/`FS_FreeImage` (TGAs come back as `PF_RGBA_32`, `img_tga.c:66`), `FS_LoadFile( path, NULL, false )`, `CoF_DescriptorValue( text, key, out, size )` (`server/sv_cof_descriptor.h`, header-only).
- Produces: `qboolean CL_CoF_Inventory12Ready( void )`; `void CL_CoF_Inventory12Init( void )`, `void CL_CoF_Inventory12Load( void *module )`, `void CL_CoF_Inventory12Unload( void )`, `void CL_CoF_Inventory12ClearState( void )`, `qboolean CL_CoF_Inventory12Message( const char *name, int size, const byte *buf )` (true = kept from client.dll), `int CL_CoF_Inventory12Api( int op, int arg, void *buf, int size )`. In `vgui_api.h`: `COF_INV_OP_GRID` (returns 12 when the grid must show, else 0), `COF_INV_OP_THREE` (1 in three-slot sections), `COF_INV_OP_SLOT` (arg 1..12, buf `cof_vgui_inv_item_t`; returns 1 item, 0 empty, -1 bad), `COF_INV_OP_COMMAND` (buf: the command text; 1 sent), `COF_INV_OP_SOUND` (buf: a sound path), `COF_INV_OP_IMAGE` (buf `cof_vgui_image_t`; 1 loaded), `COF_INV_OP_READY` (arg 1: the library has the grid); `cof_vgui_inv_item_t { char descriptor[128], picture[128], check[128], combined[64], classname[64]; int weapon; }`; `cof_vgui_image_t { const char *path; int wide, tall; unsigned char *rgba; int size; }`; member `int (*CofInventory)( int op, int arg, void *buf, int size );`. Client command `cof_inv_slots`, userinfo cvar `_cofinv`.

This task's glue has no offline harness of its own (its logic is in the Task 5 headers); it is checked by compiling now and by the live checklist (Task 10).

- [ ] **Step 1: The interface types.** In `ENG/engine/vgui_api.h`, before the `vguiapi_t` struct (next to the `COF_PANEL_*` enum), add:

```c
// Cry of Fear story co-op 12-slot inventory (engine/client/cof_inventory12.c,
// 3rdparty/freevgui/platform/xash3d-fwgs/cofinventory.cpp). vguiapi_t::CofInventory ops:
enum
{
	COF_INV_OP_GRID = 0,	// returns 12 when the 12-slot grid replaces the retail slots now, else 0
	COF_INV_OP_THREE,	// returns 1 in the story's three-slot sections (only slots 1..3)
	COF_INV_OP_SLOT,	// arg 1..12, buf cof_vgui_inv_item_t: returns 1 for an item, 0 empty, -1 bad slot
	COF_INV_OP_COMMAND,	// buf: one inventory command for the server ("inventoryequip 7"); returns 1 when sent
	COF_INV_OP_SOUND,	// buf: a sound path, played locally
	COF_INV_OP_IMAGE,	// buf cof_vgui_image_t: a TGA from the game filesystem; rgba NULL asks only the size
	COF_INV_OP_READY,	// the library has the grid (arg 1)
};

typedef struct cof_vgui_inv_item_s
{
	char	descriptor[128];	// inventoryitems/....txt, "" for an empty slot
	char	picture[128];		// inventory_picture (99x99)
	char	check[128];		// check_picture (303x99, the examine picture)
	char	combined[64];		// combined_result
	char	classname[64];		// classname
	int	weapon;			// the descriptor path names a weapon_ (the retail rule)
} cof_vgui_inv_item_t;

typedef struct cof_vgui_image_s
{
	const char	*path;
	int		wide, tall;	// filled in
	unsigned char	*rgba;		// wide * tall * 4 bytes, top row first; NULL: size only
	int		size;		// bytes at rgba
} cof_vgui_image_t;
```

and as the last member of `vguiapi_t`, after `CofPanelAction`:

```c
	// Cry of Fear story co-op 12-slot inventory, filled in by the engine
	// (cof_inventory12.c): the COF_INV_OP_* calls above. xash.dll and vgui.dll
	// must come from the same build.
	int	(*CofInventory)( int op, int arg, void *buf, int size );
```

- [ ] **Step 2: The engine table.** In `ENG/engine/client/vgui/vgui_draw.c`, beside the other local declarations (~line 440) add `int CL_CoF_Inventory12Api( int op, int arg, void *buf, int size ); // cof_inventory12.c`, and at the end of the `gEngfuncs` initializer, after `CL_CoF_PanelImageLatch,`, add:

```c
	NULL, // CofPanelAction, filled in by the support library
	CL_CoF_Inventory12Api, // CofInventory (cof_inventory12.c)
```

- [ ] **Step 3: The module.** Create `ENG/engine/client/cof_inventory12.c`:

```c
/*
cof_inventory12.c - Cry of Fear story co-op: the 12-slot inventory, client side
GPL-3.0-or-later; docs/superpowers/specs/2026-10-14-inventory-12-slots-design.md section 2.

client.dll knows six slots. When its file is the retail 1.6 one, its single
slot-string accessor is redirected to the engine's store of twelve
(cof_inventory12_patch.h), Inventory messages for slots 7..12 are kept from it
(cof_inventory12_state.h) and the UI library draws the grid
(3rdparty/freevgui/platform/xash3d-fwgs/cofinventory.cpp) through
vguiapi_t::CofInventory. With both in place the client advertises the userinfo
key _cofinv 12; the server says each map's slot count with cof_inv_slots.
*/
#include "common.h"
#include "client.h"
#include "library.h"
#include "vgui_api.h"
#include "sv_cof_descriptor.h"
#include "cof_inventory12_state.h"
#if XASH_WIN32 && XASH_X86
#include "cof_inventory12_patch.h"
#endif

#define COF_INV12_CLIENT_SHA256 "d2a04641b301804f6f449aa68265042b13adc360925b80033d417ec9f38c9c00" // client.dll, Cry of Fear 1.6

static CVAR_DEFINE( cof_inv12_userinfo, "_cofinv", "", FCVAR_USERINFO, "story co-op: \"12\" when this client has the 12-slot inventory (set by the engine)" );

static cof_inv12_slots_t cof_inv12_store;
static struct
{
#if XASH_WIN32 && XASH_X86
	cof_inv12_hook_t hook;
#endif
	qboolean patched;	// client.dll's accessor reads the store
	qboolean grid;		// the UI library has the grid
	int slots;		// the server's cof_inv_slots for this map: 12, 6, or 0 before it said
	struct { char descriptor[COF_INV12_TEXT]; cof_vgui_inv_item_t item; } cache[COF_INV12_SLOTS + 1];
} cof_inv12;

qboolean CL_CoF_Inventory12Ready( void )
{
	return cof_inv12.patched && cof_inv12.grid;
}

static void CL_CoF_Inv12Advertise( void )
{
	if( CL_CoF_Inventory12Ready( ) && Q_strcmp( cof_inv12_userinfo.string, "12" ))
		Cvar_DirectSet( &cof_inv12_userinfo, "12" ); // FCVAR_USERINFO: into the userinfo, and to the server when connected
}

#if XASH_WIN32 && XASH_X86
static char * __stdcall CL_CoF_Inv12Accessor( int slot )
{
	return CoF_Inv12Slot( &cof_inv12_store, slot );
}
#endif

// cl_game.c CL_LoadProgs: after the module is loaded, before its Initialize/Init
void CL_CoF_Inventory12Load( void *module )
{
#if XASH_WIN32 && XASH_X86
	dll_user_t *lib = (dll_user_t *)module;
	cof_inv12.patched = false;
	if( !lib || !lib->hInstance || lib->custom_loader ) return;
	if( !COM_CoF_ModuleSha256Is( lib, COF_INV12_CLIENT_SHA256 ))
	{
		Con_Reportf( "[cof-inv12] client.dll is not the retail Cry of Fear 1.6 one: six inventory slots\n" );
		return;
	}
	memset( &cof_inv12_store, 0, sizeof( cof_inv12_store ));
	if( !CoF_Inv12HookAccessor( &cof_inv12.hook, (unsigned char *)lib->hInstance, (const void *)CL_CoF_Inv12Accessor ))
	{
		Con_Printf( S_WARN "[cof-inv12] client.dll's slot accessor has unexpected bytes: six inventory slots\n" );
		return;
	}
	cof_inv12.patched = true;
	Con_Reportf( "[cof-inv12] client.dll's slot accessor reads the 12-slot store\n" );
	CL_CoF_Inv12Advertise( );
#endif
}

// cl_game.c CL_UnloadProgs: before the module is freed
void CL_CoF_Inventory12Unload( void )
{
#if XASH_WIN32 && XASH_X86
	CoF_Inv12UnhookAccessor( &cof_inv12.hook );
#endif
	cof_inv12.patched = false;
}

void CL_CoF_Inventory12ClearState( void )
{
	cof_inv12.slots = 0; // the server says it again for the next map
}

static void CL_CoF_Inv12Pickup( const char *descriptor )
{
	char nice[64];
	byte line[192];
	byte *text;
	int length;
	if( !descriptor[0] || !CoF_DescriptorSafe( descriptor ) || !( text = FS_LoadFile( descriptor, NULL, false ))) return;
	CoF_DescriptorValue( (const char *)text, "nice_name", nice, sizeof( nice ));
	Mem_Free( text );
	length = CoF_Inv12PickupLine( nice, line, sizeof( line ));
	if( length ) CL_DispatchUserMessage( "ProFont", length, line );
}

// cl_parse.c CL_ParseUserMessage, before client.dll's hook: true when the
// engine kept the message from client.dll
qboolean CL_CoF_Inventory12Message( const char *name, int size, const byte *buf )
{
	cof_inv12_message_t m;
	if( !Q_strcmp( name, "ResetHUD" ))
	{
		CoF_Inv12ResetHigh( &cof_inv12_store );
		return false;
	}
	if( Q_strcmp( name, "Inventory" )) return false;
	if( !CoF_Inv12Parse( buf, size, &m ))
	{
		Con_Printf( S_WARN "[cof-inv12] a malformed Inventory message was dropped\n" );
		return true;
	}
	switch( CoF_Inv12Route( &cof_inv12_store, &m ))
	{
	case COF_INV12_FORWARD:
		return false;
	case COF_INV12_KEPT:
		if( !m.silent ) CL_CoF_Inv12Pickup( m.descriptor );
		return true;
	default:
		Con_Printf( S_WARN "[cof-inv12] an Inventory message for slot %d was dropped\n", m.slot );
		return true;
	}
}

static void CL_CoF_Inv12Slots_f( void )
{
	int n = Q_atoi( Cmd_Argv( 1 ));
	cof_inv12.slots = n == 12 || n == 6 ? n : 0;
}

static int CL_CoF_Inv12Item( int slot, cof_vgui_inv_item_t *out )
{
	const char *d;
	if( slot < 1 || slot > COF_INV12_SLOTS ) return -1;
	d = cof_inv12_store.slot[slot];
	if( Q_strcmp( cof_inv12.cache[slot].descriptor, d )) // read each descriptor file once
	{
		cof_vgui_inv_item_t *it = &cof_inv12.cache[slot].item;
		byte *text;
		memset( it, 0, sizeof( *it ));
		Q_strncpy( cof_inv12.cache[slot].descriptor, d, sizeof( cof_inv12.cache[slot].descriptor ));
		Q_strncpy( it->descriptor, d, sizeof( it->descriptor ));
		it->weapon = Q_strstr( d, "weapon_" ) != NULL;
		if( CoF_DescriptorSafe( d ) && ( text = FS_LoadFile( d, NULL, false )))
		{
			CoF_DescriptorValue( (const char *)text, "inventory_picture", it->picture, sizeof( it->picture ));
			CoF_DescriptorValue( (const char *)text, "check_picture", it->check, sizeof( it->check ));
			CoF_DescriptorValue( (const char *)text, "combined_result", it->combined, sizeof( it->combined ));
			CoF_DescriptorValue( (const char *)text, "classname", it->classname, sizeof( it->classname ));
			Mem_Free( text );
		}
	}
	*out = cof_inv12.cache[slot].item;
	return d[0] ? 1 : 0;
}

// Only what the grid's menu sends, the way client.dll's own menu sends it.
static qboolean CL_CoF_Inv12Command( const char *text )
{
	static const char *allowed[] = { "inventoryequip ", "inventorydrop ", "inventorycombine ", "inventorydualwield ", "quickselset1 ", "quickselset2 ", "quickselset3 " };
	const char *c;
	size_t i;
	for( c = text; *c; c++ )
		if( *c == ';' || *c == '"' || *c == '\n' || *c == '\r' ) return false;
	for( i = 0; i < ARRAYSIZE( allowed ); i++ )
		if( !Q_strncmp( text, allowed[i], Q_strlen( allowed[i] )))
		{
			CL_ServerCommand( true, "%s", text );
			return true;
		}
	return false;
}

static int CL_CoF_Inv12Image( cof_vgui_image_t *image )
{
	rgbdata_t *pic;
	int bytes;
	if( !image->path || !image->path[0] || Q_strstr( image->path, ".." )) return 0;
	pic = FS_LoadImage( image->path, NULL, 0 );
	if( !pic ) return 0;
	if( pic->type != PF_RGBA_32 ) // every TGA comes out RGBA (img_tga.c)
	{
		FS_FreeImage( pic );
		return 0;
	}
	image->wide = pic->width;
	image->tall = pic->height;
	bytes = pic->width * pic->height * 4;
	if( image->rgba )
	{
		if( image->size < bytes )
		{
			FS_FreeImage( pic );
			return 0;
		}
		memcpy( image->rgba, pic->buffer, bytes );
	}
	FS_FreeImage( pic );
	return 1;
}

// vguiapi_t::CofInventory (vgui_draw.c)
int CL_CoF_Inventory12Api( int op, int arg, void *buf, int size )
{
	switch( op )
	{
	case COF_INV_OP_GRID:
		return cof_inv12.slots == 12 && CL_CoF_Inventory12Ready( ) ? 12 : 0;
	case COF_INV_OP_THREE:
		return cof_inv12_store.three ? 1 : 0;
	case COF_INV_OP_SLOT:
		return buf && size == sizeof( cof_vgui_inv_item_t ) ? CL_CoF_Inv12Item( arg, (cof_vgui_inv_item_t *)buf ) : -1;
	case COF_INV_OP_COMMAND:
		return buf && CL_CoF_Inv12Command( (const char *)buf ) ? 1 : 0;
	case COF_INV_OP_SOUND:
		if( !buf || Q_strstr( (const char *)buf, ".." )) return 0;
		S_StartLocalSound( (const char *)buf, 1.0f, false );
		return 1;
	case COF_INV_OP_IMAGE:
		return buf && size == sizeof( cof_vgui_image_t ) ? CL_CoF_Inv12Image( (cof_vgui_image_t *)buf ) : 0;
	case COF_INV_OP_READY:
		cof_inv12.grid = arg != 0;
		CL_CoF_Inv12Advertise( );
		return 1;
	}
	return 0;
}

void CL_CoF_Inventory12Init( void )
{
	Cvar_RegisterVariable( &cof_inv12_userinfo );
	Cmd_AddCommand( "cof_inv_slots", CL_CoF_Inv12Slots_f, "story co-op: the server's inventory slot count for this map" );
	CL_CoF_Inv12Advertise( ); // the UI library may have reported before the cvar existed
}
```

Notes for the implementer: `CVAR_DEFINE( name, "cvar", value, flags, description )` and `Cvar_RegisterVariable` are the forms `cl_main.c` uses (line 36); if `Cvar_DirectSet` on an `FCVAR_USERINFO` cvar does not update `cls.userinfo` and forward it while connected, call `Info_SetValueForKey( cls.userinfo, "_cofinv", "12", sizeof( cls.userinfo ))` and, when `cls.state >= ca_connected`, `CL_ServerCommand( true, "setinfo \"_cofinv\" \"12\"\n" )` instead. If the UI library reports `COF_INV_OP_READY` before `CL_CoF_Inventory12Init` registered the cvar, `CL_CoF_Inv12Advertise` must not touch the unregistered cvar: guard it with `if( !cof_inv12_userinfo.name ) return;` or the equivalent the cvar API offers.

- [ ] **Step 4: Wire it in.**
  - `cl_parse.c`, `CL_ParseUserMessage`: after `CL_CoF_HudRemakeMessage( clgame.msg[i].name, iSize, pbuf );` add:

```c
	// Cry of Fear story co-op: Inventory slots 7..12 never reach client.dll
	if( CL_CoF_Inventory12Message( clgame.msg[i].name, iSize, pbuf ))
		return;
```

  - `cl_game.c`, `CL_LoadProgs`: after `CL_CoF_EnterHookModule( clgame.hInstance, name );` add `CL_CoF_Inventory12Load( clgame.hInstance );`. `CL_UnloadProgs`: before the `COM_FreeLibrary( clgame.hInstance )` call add `CL_CoF_Inventory12Unload();`.
  - `cl_main.c`, `CL_ClearState`: next to `CL_CoF_HudRemakeReset();` add `CL_CoF_Inventory12ClearState();`. `CL_InitLocal`: next to the other CoF init calls add `{ void CL_CoF_Inventory12Init( void ); CL_CoF_Inventory12Init(); }`.
  - `client.h`: declare `void CL_CoF_Inventory12Load( void *module );`, `void CL_CoF_Inventory12Unload( void );`, `void CL_CoF_Inventory12ClearState( void );`, `qboolean CL_CoF_Inventory12Message( const char *name, int size, const byte *buf );` beside the other `CL_CoF_*` declarations.
- [ ] **Step 5: Compile.** From `ROOT`: `cmd /d /c tools\build_enhanced.cmd > evidence\inventory-grid-20261013\build-task6.log 2>&1`. Expected: exit code 0; the log shows `cof_inventory12.c` and `sv_cof_inventory12.c` compiled. If waf did not pick up the new files, stop and report (do not reconfigure without the user). Fix warnings in the new code.
- [ ] **Step 6: Run the offline suite.** `python -m unittest tests.test_enhanced_inventory12 tests.test_enhanced_client_transition -v`. Expected: OK.
- [ ] **Step 7: Commit** (see Global Constraints). Engine: "Story co-op: the client's 12-slot inventory glue and vguiapi_t::CofInventory".

---

### Task 7: Grid layout and click rules (pure C++)

**Files:**
- Create: `ENG/3rdparty/freevgui/platform/xash3d-fwgs/cofinventory_logic.h`
- Create: `ROOT/tests/engine/cof_inventory_logic_test.cpp`
- Modify: `ROOT/tests/native.py` (add `run_native_cpp`), `ROOT/tests/test_enhanced_inventory12.py`

**Interfaces:**
- Produces (namespace `cofinv`): constants `kSlots` (12), `kBox` (72), `kStride` (78), `kLeft` (36), `kRowTop[3]` ({68, 146, 262}), `kGridWide` (373), `kGridTall` (480), `kCheckX/Y/Wide/Tall` (31, 352, 303, 99), `kMenuX` (350), `kMenuButton` (42), `kMenuWide` (126), `kArtItems` (126), `kArtWeapons` (210); `struct Rect { int x, y, w, h; bool contains( int, int ) const; }`; `Rect SlotRect( int slot )`; `int SlotAt( int x, int y, int visible )`; `struct Item { bool present, weapon; char combined[64], classname[64]; }`; `enum Action { kNone, kEquip, kCombine, kDrop, kDualWield, kQuick1, kQuick2, kQuick3 }`; `struct Menu { int slot; bool weapon; int x, y; }`; `Menu PlaceMenu( int slot, bool weapon )`; `Rect MenuArt( const Menu & )`; `Action MenuHit( const Menu &, int x, int y )`; `struct State { int menu; bool weapon; int combineFrom, dualFrom; }`; `struct Result { char command[96]; int sound; }` with `kSoundNone/kSoundToggle/kSoundNo`; `Result ClickSlot( State &, int slot, const Item items[13] )`; `Result ClickMenu( State &, Action )`; `void ClickOutside( State & )`; `void Sync( State &, const Item items[13], int visible )`; `void Reset( State & )`. All coordinates are in the 748×480 background's pixels. Task 9 copies the layout constants and a test compares them.

- [ ] **Step 1: The C++ runner.** Add to `ROOT/tests/native.py`:

```python
def run_native_cpp(case: unittest.TestCase, harness: str, files: dict, timeout: int = 120) -> str:
    """The same for a C++ harness (MSVC /TP, C++14, no exceptions: as FreeVGUI builds)."""
    if not VCVARS.exists() and not shutil.which('c++'):
        case.skipTest('a C++ compiler is required for the offline UI harness')
    with tempfile.TemporaryDirectory(prefix='cof-native-cpp-') as tmp:
        work = Path(tmp)
        for name, text in files.items():
            (work / name).write_text(text)
        shutil.copyfile(ROOT / 'tests/engine' / harness, work / 'harness.cpp')
        if os.name == 'nt':
            (work / 'build.cmd').write_text(
                f'@call "{VCVARS}" amd64_x86 >nul\n'
                '@cl /nologo /W4 /WX /wd4100 /D_CRT_SECURE_NO_WARNINGS /TP harness.cpp /Fe:native-test.exe\n'
                '@if errorlevel 1 exit /b %errorlevel%\n'
                '@.\\native-test.exe\n')
            command = ['cmd.exe', '/d', '/c', str(work / 'build.cmd')]
        else:
            command = ['sh', '-c', 'c++ -std=c++14 -Wall -Wextra -Werror -Wno-unused-parameter harness.cpp -o native-test && ./native-test']
        result = subprocess.run(command, cwd=work, capture_output=True, text=True, timeout=timeout)
        case.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        return result.stdout
```

- [ ] **Step 2: Failing test.** Create `ROOT/tests/engine/cof_inventory_logic_test.cpp`:

```cpp
// The 12-slot grid's layout and click rules (cofinventory_logic.h).
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "logic_under_test.h"
using namespace cofinv;

static Item items[kSlots + 1];

static void Put( int slot, bool weapon, const char *combined, const char *classname )
{
	items[slot].present = true;
	items[slot].weapon = weapon;
	strcpy( items[slot].combined, combined );
	strcpy( items[slot].classname, classname );
}
static bool Overlap( const Rect &a, const Rect &b ) { return a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h && b.y < a.y + a.h; }

int main()
{
	// layout: 4x3 in the left panel (x 6..366), clear of each other, of the description box and of the menu column
	assert( SlotRect( 1 ).x == 36 && SlotRect( 4 ).x == 270 && SlotRect( 5 ).y == 146 && SlotRect( 9 ).y == 262 && SlotRect( 12 ).x == 270 );
	for( int s = 1; s <= kSlots; s++ )
	{
		Rect r = SlotRect( s );
		assert( r.w == kBox && r.h == kBox && r.x >= 8 && r.x + r.w <= 366 && r.y + r.h <= kCheckY );
		assert( r.x + r.w <= kMenuX - 5 );
		for( int t = s + 1; t <= kSlots; t++ ) assert( !Overlap( r, SlotRect( t )));
	}
	assert( SlotAt( 40, 70, 12 ) == 1 && SlotAt( 300, 300, 12 ) == 12 && SlotAt( 300, 300, 3 ) == 0 );
	assert( SlotAt( 110, 100, 12 ) == 0 ); // the gap between boxes 1 and 2

	// the menu: beside the grid, never over a box, the art on the background
	for( int s = 1; s <= kSlots; s++ )
		for( int w = 0; w < 2; w++ )
		{
			Menu m = PlaceMenu( s, w != 0 );
			Rect a = MenuArt( m );
			assert( a.y >= 0 && a.y + a.h <= kGridTall && a.x + a.w <= 748 );
			for( int t = 1; t <= kSlots; t++ ) assert( !Overlap( a, SlotRect( t )));
		}
	Menu m = PlaceMenu( 6, true );
	assert( MenuHit( m, m.x + 10, m.y + 10 ) == kEquip && MenuHit( m, m.x + 10, m.y + 50 ) == kCombine );
	assert( MenuHit( m, m.x + 10, m.y + 90 ) == kDrop && MenuHit( m, m.x + 10, m.y + 130 ) == kDualWield );
	assert( MenuHit( m, m.x + 10, m.y + 180 ) == kQuick1 && MenuHit( m, m.x + 100, m.y + 180 ) == kQuick3 );
	assert( MenuHit( m, m.x + 130, m.y + 180 ) == kNone && MenuHit( m, m.x - 1, m.y ) == kNone );
	Menu item = PlaceMenu( 3, false );
	assert( MenuHit( item, item.x + 10, item.y + 130 ) == kNone ); // items have no DUAL WIELD or quick row

	// the slot-6 bug: with slot 5's menu open, a click on slot 6 selects slot 6
	Put( 5, false, "", "item_key" );
	Put( 6, true, "glockwithtaclight", "weapon_glock" );
	State s;
	Result r = ClickSlot( s, 5, items );
	assert( s.menu == 5 && r.sound == kSoundToggle && !r.command[0] );
	r = ClickSlot( s, 6, items );
	assert( s.menu == 6 && s.weapon && r.sound == kSoundToggle );
	r = ClickMenu( s, kEquip );
	assert( !strcmp( r.command, "inventoryequip 6" ) && s.menu == 0 );

	// slots 7..12 work like the rest
	Put( 11, false, "", "item_key" );
	ClickSlot( s, 11, items );
	r = ClickMenu( s, kDrop );
	assert( !strcmp( r.command, "inventorydrop 11" ));
	Put( 9, true, "", "weapon_shotgun" );
	ClickSlot( s, 9, items );
	r = ClickMenu( s, kQuick2 );
	assert( !strcmp( r.command, "quickselset2 9" ));
	ClickSlot( s, 5, items ); // an item: no quick select
	r = ClickMenu( s, kQuick1 );
	assert( !r.command[0] && s.menu == 0 );

	// combine across the 6/7 line: the target needs a result, the source's is sent
	Put( 7, false, "glockwithtaclight", "item_glocktaclight" );
	ClickSlot( s, 6, items );
	r = ClickMenu( s, kCombine );
	assert( s.combineFrom == 6 && s.menu == 0 && !r.command[0] );
	r = ClickSlot( s, 7, items );
	assert( !strcmp( r.command, "inventorycombine 6 7 glockwithtaclight" ) && !s.combineFrom );
	ClickSlot( s, 6, items ); ClickMenu( s, kCombine );
	r = ClickSlot( s, 6, items ); // onto itself
	assert( r.sound == kSoundNo && !r.command[0] && !s.combineFrom );
	ClickSlot( s, 6, items ); ClickMenu( s, kCombine );
	r = ClickSlot( s, 5, items ); // no combined_result
	assert( r.sound == kSoundNo && !r.command[0] );

	// dual wield (the server refuses it in multiplayer; the screen still asks, as the retail one)
	ClickSlot( s, 6, items );
	ClickMenu( s, kDualWield );
	r = ClickSlot( s, 9, items );
	assert( !strcmp( r.command, "inventorydualwield 6 9" ) && !s.dualFrom );
	ClickSlot( s, 6, items ); ClickMenu( s, kDualWield );
	r = ClickSlot( s, 6, items );
	assert( r.sound == kSoundNo && !r.command[0] );

	// an empty box closes the menu without a sound
	ClickSlot( s, 6, items );
	r = ClickSlot( s, 10, items );
	assert( s.menu == 0 && r.sound == kSoundNone && !r.command[0] );

	// Sync: the item under an open menu or a pending combine goes away, or a three-slot section hides it
	ClickSlot( s, 11, items );
	items[11].present = false;
	Sync( s, items, kSlots );
	assert( s.menu == 0 );
	ClickSlot( s, 6, items ); ClickMenu( s, kCombine );
	Sync( s, items, 3 );
	assert( s.combineFrom == 0 );
	ClickSlot( s, 6, items ); ClickMenu( s, kDualWield );
	items[6].present = false;
	Sync( s, items, kSlots );
	assert( s.dualFrom == 0 );
	items[6].present = true;

	// a click outside closes the menu; Reset clears everything
	ClickSlot( s, 6, items );
	ClickOutside( s );
	assert( s.menu == 0 );
	ClickSlot( s, 6, items ); ClickMenu( s, kCombine );
	Reset( s );
	assert( !s.menu && !s.combineFrom && !s.dualFrom );
	puts( "inventory grid logic passed" );
	return 0;
}
```

Add to `ROOT/tests/test_enhanced_inventory12.py` (and add `run_native_cpp` to its `from tests.native import` line; `FREEVGUI = ENGINE.parent / '3rdparty/freevgui/platform/xash3d-fwgs'` beside the other paths):

```python
class GridLogicTests(unittest.TestCase):
    def test_layout_and_clicks(self):
        out = run_native_cpp(self, 'cof_inventory_logic_test.cpp',
                             {'logic_under_test.h': (FREEVGUI / 'cofinventory_logic.h').read_text()})
        self.assertIn('inventory grid logic passed', out)
```

- [ ] **Step 3: Run it to see it fail.** `python -m unittest tests.test_enhanced_inventory12 -v`. Expected: `test_layout_and_clicks` FAIL (`FileNotFoundError`).

- [ ] **Step 4: The rules.** Create `ENG/3rdparty/freevgui/platform/xash3d-fwgs/cofinventory_logic.h`:

```cpp
// SPDX-License-Identifier: BSD-3-Clause
// Cry of Fear story co-op: the 12-slot inventory grid's layout and click rules
// (cofinventory.cpp). Pure: no VGUI, no engine; the offline harness compiles it
// as it is. Coordinates are the client's own layout space, the pixels of the
// 748x480 inventory background. The retail menu rules it follows are the
// client's SlotClicked (10044670) and popup actions (10043130):
// evidence/inventory-grid-20261013/client-inventory-re.md section 3.
#ifndef COFINVENTORY_LOGIC_H
#define COFINVENTORY_LOGIC_H
#include <stdio.h>
#include <string.h>

namespace cofinv
{
const int kSlots = 12;
const int kBox = 72, kStride = 78, kLeft = 36;
const int kRowTop[3] = { 68, 146, 262 };	// BAG: slots 1..8; POCKETS: 9..12
const int kGridWide = 373, kGridTall = 480;	// the generated left-half art (tools/make_inventory12_art.py)
const int kCheckX = 31, kCheckY = 352, kCheckWide = 303, kCheckTall = 99; // the examine picture
const int kMenuX = 350;				// the item menu opens right of the grid: it never covers a box
const int kMenuButton = 42, kMenuWide = 126;
const int kArtItems = 126, kArtWeapons = 210;	// inventory_items.tga / inventory_weapons.tga, drawn 5 px up and left

struct Rect
{
	int x, y, w, h;
	bool contains( int px, int py ) const { return px >= x && py >= y && px < x + w && py < y + h; }
};

inline Rect SlotRect( int slot )
{
	int i = slot - 1;
	Rect r = { kLeft + ( i % 4 ) * kStride, kRowTop[i / 4], kBox, kBox };
	return r;
}

// the box under a point, or 0; only the first `visible` boxes exist (3 in the story's three-slot sections)
inline int SlotAt( int px, int py, int visible )
{
	for( int s = 1; s <= visible && s <= kSlots; s++ )
		if( SlotRect( s ).contains( px, py )) return s;
	return 0;
}

struct Item { bool present, weapon; char combined[64], classname[64]; };

enum Action { kNone = 0, kEquip, kCombine, kDrop, kDualWield, kQuick1, kQuick2, kQuick3 };

struct Menu { int slot; bool weapon; int x, y; };

inline Menu PlaceMenu( int slot, bool weapon )
{
	int art = weapon ? kArtWeapons : kArtItems;
	int y = SlotRect( slot ).y;
	if( y - 5 + art > kGridTall ) y = kGridTall - art + 5; // keep the art on the background
	Menu m = { slot, weapon, kMenuX, y };
	return m;
}

inline Rect MenuArt( const Menu &m )
{
	Rect r = { m.x - 5, m.y - 5, kMenuWide, m.weapon ? kArtWeapons : kArtItems };
	return r;
}

// EQUIP (USE for items), COMBINE, DROP stacked 42 px apart; for weapons DUAL
// WIELD below them, then the quick-select 1/2/3 buttons in one row of 42x42
inline Action MenuHit( const Menu &m, int px, int py )
{
	int dx = px - m.x, dy = py - m.y;
	if( dx < 0 || dy < 0 || dx >= kMenuWide ) return kNone;
	if( dy < 3 * kMenuButton ) return (Action)( kEquip + dy / kMenuButton );
	if( !m.weapon ) return kNone;
	if( dy < 4 * kMenuButton ) return kDualWield;
	if( dy < 5 * kMenuButton && dx < 3 * kMenuButton ) return (Action)( kQuick1 + dx / kMenuButton );
	return kNone;
}

struct State
{
	int menu = 0;		// the box whose menu is open
	bool weapon = false;	// that box holds a weapon
	int combineFrom = 0;	// COMBINE was chosen here; the next box completes it
	int dualFrom = 0;	// DUAL WIELD was chosen here
};

enum { kSoundNone = 0, kSoundToggle, kSoundNo };

struct Result
{
	char command[96];	// for the server, "" for none
	int sound;		// kSound*
};

inline Result Nothing()
{
	Result r;
	r.command[0] = 0;
	r.sound = kSoundNone;
	return r;
}

inline Result ClickSlot( State &s, int slot, const Item items[kSlots + 1] )
{
	Result r = Nothing();
	if( s.dualFrom )
	{
		int from = s.dualFrom;
		s.dualFrom = 0;
		if( slot == from ) r.sound = kSoundNo;
		else if( items[slot].present && items[slot].classname[0] )
			snprintf( r.command, sizeof( r.command ), "inventorydualwield %d %d", from, slot );
		return r;
	}
	if( s.combineFrom )
	{
		int from = s.combineFrom;
		s.combineFrom = 0;
		if( slot != from && items[slot].present && items[slot].combined[0] )
			snprintf( r.command, sizeof( r.command ), "inventorycombine %d %d %s", from, slot, items[from].combined );
		else r.sound = kSoundNo;
		return r;
	}
	if( !items[slot].present )
	{
		s.menu = 0;
		return r;
	}
	s.menu = slot; // a box beside an open menu opens its own: the retail menu covered it
	s.weapon = items[slot].weapon;
	r.sound = kSoundToggle;
	return r;
}

inline Result ClickMenu( State &s, Action a )
{
	Result r = Nothing();
	int slot = s.menu;
	if( !slot || a == kNone ) return r;
	s.menu = 0;
	switch( a )
	{
	case kEquip: snprintf( r.command, sizeof( r.command ), "inventoryequip %d", slot ); break;
	case kDrop: snprintf( r.command, sizeof( r.command ), "inventorydrop %d", slot ); break;
	case kCombine: s.combineFrom = slot; break;
	case kDualWield: if( s.weapon ) s.dualFrom = slot; break;
	case kQuick1: case kQuick2: case kQuick3:
		if( s.weapon ) snprintf( r.command, sizeof( r.command ), "quickselset%d %d", (int)( a - kQuick1 ) + 1, slot );
		break;
	default: break;
	}
	return r;
}

inline void ClickOutside( State &s ) { s.menu = 0; }

// every frame: a menu or a pending combine / dual wield whose item went away (used,
// shared, dropped by the server) or that a three-slot section hides is cancelled
inline void Sync( State &s, const Item items[kSlots + 1], int visible )
{
	if( s.menu && ( s.menu > visible || !items[s.menu].present )) s.menu = 0;
	if( s.combineFrom && ( s.combineFrom > visible || !items[s.combineFrom].present )) s.combineFrom = 0;
	if( s.dualFrom && ( s.dualFrom > visible || !items[s.dualFrom].present )) s.dualFrom = 0;
}

inline void Reset( State &s ) { s = State(); }
} // namespace cofinv

#endif // COFINVENTORY_LOGIC_H
```

- [ ] **Step 5: Run it to see it pass.** `python -m unittest tests.test_enhanced_inventory12 -v`. Expected: OK.
- [ ] **Step 6: Commit** (see Global Constraints). FreeVGUI (`git -C ENG/3rdparty/freevgui`): "Story co-op: the 12-slot grid's layout and click rules". Launcher: "Test the 12-slot grid's layout and click rules".

---

### Task 8: The grid and the item menu in FreeVGUI

**Files:**
- Create: `ENG/3rdparty/freevgui/platform/xash3d-fwgs/cofinventory.cpp`
- Modify: `ENG/3rdparty/freevgui/platform/xash3d-fwgs/support.h` (~line 77), `ENG/3rdparty/freevgui/platform/xash3d-fwgs/app.cpp` (`XashPaint` ~line 96, `VGUI_SUPPORT_INIT` ~line 148)

**Interfaces:**
- Consumes: Task 6 `vguiapi_t::CofInventory` and the `COF_INV_OP_*` / `cof_vgui_inv_item_t` / `cof_vgui_image_t` definitions (`engine/vgui_api.h`); Task 7 `cofinventory_logic.h`; existing FreeVGUI `CofPanels_Class`, `CofPanels_ClassName` (returns the decorated RTTI name, e.g. `.?AVColorButton@@`), `CofMemReadable` (`cofmem.cpp`), `App::getInstance()->getCursorPos`, `App::wasMousePressed( MOUSE_LEFT, nullptr )`, `Panel::screenToLocal`, `getSurfaceBase()->createNewTextureID()`, the protected `drawSetTextureRGBA/drawSetTexture/drawSetColor/drawTexturedRect/drawFilledRect`, `InputSignalAdapter`.
- Produces: `void vgui::CofInventory_Frame( Panel *root )`, called every painted frame after `CofBackdrop_Frame`. The library reports `COF_INV_OP_READY` once at `VGUI_SUPPORT_INIT`.
- The client's `CRECBInventory` (1.6 client.dll, `new(0x1C4)`) children used: background `+0x150` (`.?AVImagePanel@vgui@@`); hidden every frame while the grid is on: slot buttons `+0x15C..+0x170` (`.?AVColorButton@@`), slot pictures `+0x120..+0x134` and hover picture `+0x140` (`.?AVCImageLabel@@`), menu art `+0xDC` (`.?AVCImageLabel@@`), menu buttons `+0x100..+0x118` (`.?AVColorButton@@`), highlight `+0xF8` (`.?AVPanel@vgui@@`). Client RE §1, §2.2.

This task has no offline harness (the rules are Task 7's); it is checked by the build here and by the live checklist (Task 10).

- [ ] **Step 1: Declare the hook.** In `support.h`, after `void CofBackdrop_Frame( Panel *root );`, add:

```cpp
// Cry of Fear story co-op: the 12-slot inventory grid over the client's inventory
// panel, every painted frame after CofBackdrop_Frame (cofinventory.cpp)
void CofInventory_Frame( Panel *root );
```

- [ ] **Step 2: Call it.** In `app.cpp`, `XashPaint`, after `CofBackdrop_Frame( panel );`:

```cpp
	// Cry of Fear story co-op: the 12-slot inventory grid (cofinventory.cpp)
	CofInventory_Frame( panel );
```

and at the end of `VGUI_SUPPORT_INIT`, after `api->CofPanelAction = CofPanels_Action;`:

```cpp
	// Cry of Fear story co-op: this library draws the 12-slot inventory grid
	// (cofinventory.cpp); the engine advertises it to servers (cof_inventory12.c)
	if( api->CofInventory )
		api->CofInventory( COF_INV_OP_READY, 1, nullptr, 0 );
```

- [ ] **Step 3: The panels.** Create `ENG/3rdparty/freevgui/platform/xash3d-fwgs/cofinventory.cpp`:

```cpp
// SPDX-License-Identifier: BSD-3-Clause
// Cry of Fear story co-op: the 12-slot inventory grid inside the client's inventory
// (docs/superpowers/specs/2026-10-14-inventory-12-slots-design.md, section 2.2).
//
// On a map the server runs at 12 slots (vguiapi_t::CofInventory, COF_INV_OP_GRID)
// the left half of CRECBInventory - its six slot buttons and pictures, the hover
// picture and the item menu - is hidden every frame, and two panels of ours are
// added last to its 748x480 background, so they are painted and hit-tested above
// every client child: the grid (the generated inventory12_left art, 12 boxes, the
// examine picture) and the item menu beside it. The right half (QUICK SELECT,
// NOTES, CURRENT OBJECTIVE) stays the client's. The client's children are found at
// their offsets in the 1.6 client.dll's CRECBInventory and checked by class (RTTI)
// first; anything unexpected leaves the retail screen alone. Commands, sounds and
// images go through the engine. Layout and click rules: cofinventory_logic.h.
#include "support.h"
#include "cofinventory_logic.h"
#include "app.h"
#include "panel.h"
#include "signals.h"
#include "surface.h"
#include <string.h>

using namespace vgui;

namespace vgui
{
bool CofMemReadable( const void *ptr, size_t size ); // cofmem.cpp
}

namespace
{
int Engine( int op, int arg, void *buf, int size )
{
	return g_engine && g_engine->CofInventory ? g_engine->CofInventory( op, arg, buf, size ) : 0;
}

void Send( const char *command )
{
	if( command[0] )
		Engine( COF_INV_OP_COMMAND, 0, (void *)command, (int)strlen( command ) + 1 );
}

void Sound( int which )
{
	const char *path = which == cofinv::kSoundToggle ? "inventory/inventory_toggle.wav" :
		which == cofinv::kSoundNo ? "inventory/inventory_no.wav" : nullptr;
	if( path )
		Engine( COF_INV_OP_SOUND, 0, (void *)path, (int)strlen( path ) + 1 );
}

// what the grid shows this frame
struct Model
{
	int visible = cofinv::kSlots;		// 12, or 3 in the story's three-slot sections
	cofinv::Item items[cofinv::kSlots + 1] = {};
	cof_vgui_inv_item_t detail[cofinv::kSlots + 1] = {};
	cofinv::State state;
	int hover = 0;				// the box under the cursor
	int blink = 0;				// painted frames, for the combine / dual wield source
};
Model g_model;

void Apply( const cofinv::Result &r )
{
	Send( r.command );
	Sound( r.sound );
}

// One VGUI texture per image path, made the first time it is drawn and then
// kept: the engine keeps VGUI textures until DrawShutdown.
struct Texture { char path[128]; int id; };
Texture g_textures[128];
int g_textureCount;

class Base : public Panel
{
public:
	Base() : Panel( 0, 0, 1, 1 ) { setPaintBackgroundEnabled( false ); }

	// the cursor in this panel's coordinates; false when it is outside
	bool cursor( int &x, int &y )
	{
		int w, h;
		App::getInstance()->getCursorPos( x, y );
		screenToLocal( x, y );
		getSize( w, h );
		return x >= 0 && y >= 0 && x < w && y < h;
	}

protected:
	int texture( const char *path )
	{
		if( !path || !path[0] ) return 0;
		for( int i = 0; i < g_textureCount; i++ )
			if( !strcmp( g_textures[i].path, path )) return g_textures[i].id;
		if( g_textureCount == (int)( sizeof( g_textures ) / sizeof( g_textures[0] ))) return 0;
		Texture &t = g_textures[g_textureCount++];
		strncpy( t.path, path, sizeof( t.path ) - 1 );
		t.path[sizeof( t.path ) - 1] = 0;
		t.id = 0; // tried once: a missing image is not asked for again
		cof_vgui_image_t image;
		memset( &image, 0, sizeof( image ));
		image.path = t.path;
		if( Engine( COF_INV_OP_IMAGE, 0, &image, sizeof( image )) != 1 || image.wide <= 0 || image.tall <= 0 ) return 0;
		image.size = image.wide * image.tall * 4;
		image.rgba = new unsigned char[image.size];
		if( Engine( COF_INV_OP_IMAGE, 0, &image, sizeof( image )) == 1 )
		{
			t.id = getSurfaceBase()->createNewTextureID();
			drawSetTextureRGBA( t.id, (const char *)image.rgba, image.wide, image.tall );
		}
		delete[] image.rgba;
		return t.id;
	}

	bool image( const char *path, int x, int y, int w, int h )
	{
		int id = texture( path );
		if( !id ) return false;
		drawSetTexture( id );
		drawSetColor( 255, 255, 255, 0 ); // VGUI alpha is inverted: 0 is opaque
		drawTexturedRect( x, y, x + w, y + h );
		return true;
	}

	void outline( const cofinv::Rect &r, int red, int green, int blue, int alpha )
	{
		drawSetColor( red, green, blue, alpha );
		drawFilledRect( r.x, r.y, r.x + r.w, r.y + 1 );
		drawFilledRect( r.x, r.y + r.h - 1, r.x + r.w, r.y + r.h );
		drawFilledRect( r.x, r.y, r.x + 1, r.y + r.h );
		drawFilledRect( r.x + r.w - 1, r.y, r.x + r.w, r.y + r.h );
	}
};

class Grid : public Base
{
public:
	void paint() override
	{
		const char *art = g_model.visible == 3 ? "gfx/vgui/inventory12_left_three.tga" : "gfx/vgui/inventory12_left.tga";
		if( !image( art, 0, 0, cofinv::kGridWide, cofinv::kGridTall ))
		{
			// the art was not generated (tools/make_inventory12_art.py): a plain panel and plain boxes
			drawSetColor( 8, 8, 8, 30 );
			drawFilledRect( 8, 64, 366, 466 );
			for( int s = 1; s <= g_model.visible; s++ ) outline( cofinv::SlotRect( s ), 100, 100, 100, 0 );
		}
		for( int s = 1; s <= g_model.visible; s++ )
		{
			cofinv::Rect r = cofinv::SlotRect( s );
			if( g_model.items[s].present ) image( g_model.detail[s].picture, r.x, r.y, r.w, r.h );
			if( s == g_model.state.combineFrom || s == g_model.state.dualFrom )
				outline( r, 200, 30, 30, ( g_model.blink / 15 ) % 2 ? 140 : 0 ); // blinks, as the retail highlight
			else if( s == g_model.state.menu || ( s == g_model.hover && g_model.items[s].present ))
				outline( r, 255, 255, 255, 120 );
		}
		int shown = g_model.hover ? g_model.hover : g_model.state.menu;
		if( shown && g_model.items[shown].present )
			image( g_model.detail[shown].check, cofinv::kCheckX, cofinv::kCheckY, cofinv::kCheckWide, cofinv::kCheckTall );
	}
};

class GridInput : public InputSignalAdapter
{
public:
	explicit GridInput( Grid *grid ) : m_grid( grid ) {}
	void cursorMoved( int, int, Panel * ) override
	{
		int x, y;
		g_model.hover = m_grid->cursor( x, y ) ? cofinv::SlotAt( x, y, g_model.visible ) : 0;
	}
	void cursorExited( Panel * ) override { g_model.hover = 0; }
	void mousePressed( MouseCode code, Panel * ) override
	{
		int x, y;
		if( code != MOUSE_LEFT || !m_grid->cursor( x, y )) return;
		int slot = cofinv::SlotAt( x, y, g_model.visible );
		if( slot ) Apply( cofinv::ClickSlot( g_model.state, slot, g_model.items ));
		else cofinv::ClickOutside( g_model.state );
	}
private:
	Grid *m_grid;
};

class ItemMenu : public Base
{
public:
	cofinv::Menu menu = {};
	void paint() override
	{
		int w, h;
		getSize( w, h );
		image( menu.weapon ? "gfx/vgui/inventory_weapons.tga" : "gfx/vgui/inventory_items.tga", 0, 0, w, h );
	}
};

class MenuInput : public InputSignalAdapter
{
public:
	explicit MenuInput( ItemMenu *menu ) : m_menu( menu ) {}
	void mousePressed( MouseCode code, Panel * ) override
	{
		int x, y;
		if( code != MOUSE_LEFT || !m_menu->cursor( x, y )) return;
		cofinv::Rect art = cofinv::MenuArt( m_menu->menu );
		Apply( cofinv::ClickMenu( g_model.state, cofinv::MenuHit( m_menu->menu, x + art.x, y + art.y )));
	}
private:
	ItemMenu *m_menu;
};

// The client's CRECBInventory children, at their offsets in the 1.6 client.dll.
struct Child { int offset; const char *rtti; };
const Child kBackground = { 0x150, ".?AVImagePanel@vgui@@" };
const Child kHide[] =
{
	{ 0x15C, ".?AVColorButton@@" }, { 0x160, ".?AVColorButton@@" }, { 0x164, ".?AVColorButton@@" },	// slot buttons 1..6
	{ 0x168, ".?AVColorButton@@" }, { 0x16C, ".?AVColorButton@@" }, { 0x170, ".?AVColorButton@@" },
	{ 0x120, ".?AVCImageLabel@@" }, { 0x124, ".?AVCImageLabel@@" }, { 0x128, ".?AVCImageLabel@@" },	// slot pictures 1..6
	{ 0x12C, ".?AVCImageLabel@@" }, { 0x130, ".?AVCImageLabel@@" }, { 0x134, ".?AVCImageLabel@@" },
	{ 0x140, ".?AVCImageLabel@@" },	// the hover (examine) picture
	{ 0x0DC, ".?AVCImageLabel@@" },	// the item menu's art
	{ 0x100, ".?AVColorButton@@" }, { 0x104, ".?AVColorButton@@" }, { 0x108, ".?AVColorButton@@" },	// its seven buttons
	{ 0x10C, ".?AVColorButton@@" }, { 0x110, ".?AVColorButton@@" }, { 0x114, ".?AVColorButton@@" },
	{ 0x118, ".?AVColorButton@@" },
	{ 0x0F8, ".?AVPanel@vgui@@" },	// the combine / dual wield highlight
};
const int kHideCount = (int)( sizeof( kHide ) / sizeof( kHide[0] ));

struct Ours
{
	Panel *inventory = nullptr;	// the CRECBInventory this was built for
	Panel *hide[kHideCount] = {};
	bool was[kHideCount] = {};	// their visibility before the grid hid them
	bool hidden = false;
	Grid *grid = nullptr;
	ItemMenu *menu = nullptr;
};
Ours g_ours;

Panel *ChildAt( Panel *inventory, const Child &c )
{
	char name[128];
	const char *field = (const char *)inventory + c.offset;
	if( !CofMemReadable( field, sizeof( Panel * ))) return nullptr;
	Panel *child = *(Panel * const *)field;
	if( !child || !CofMemReadable( child, sizeof( void * ))) return nullptr;
	return !strcmp( CofPanels_ClassName( child, name, sizeof( name )), c.rtti ) ? child : nullptr;
}

// the client's inventory panel below its viewport (root -> viewport -> panel), shown or not
Panel *FindInventory( Panel *root )
{
	if( !root ) return nullptr;
	for( int i = 0; i < root->getChildCount(); i++ )
	{
		Panel *viewport = root->getChild( i );
		if( !viewport ) continue;
		for( int j = 0; j < viewport->getChildCount(); j++ )
		{
			Panel *p = viewport->getChild( j );
			if( p && CofPanels_Class( p ) == COF_PANEL_INVENTORY ) return p;
		}
	}
	return nullptr;
}

bool Bind( Panel *inventory )
{
	Panel *hide[kHideCount];
	if( !CofMemReadable( inventory, 0x1C4 )) return false;
	Panel *background = ChildAt( inventory, kBackground );
	if( !background ) return false;
	for( int i = 0; i < kHideCount; i++ )
		if( !( hide[i] = ChildAt( inventory, kHide[i] ))) return false;
	memcpy( g_ours.hide, hide, sizeof( hide ));
	g_ours.grid = new Grid();
	g_ours.grid->setParent( background );	// added last: above every client child
	g_ours.grid->setBounds( 0, 0, cofinv::kGridWide, cofinv::kGridTall );
	g_ours.grid->addInputSignal( new GridInput( g_ours.grid ));
	g_ours.grid->setVisible( false );
	g_ours.menu = new ItemMenu();
	g_ours.menu->setParent( background );
	g_ours.menu->addInputSignal( new MenuInput( g_ours.menu ));
	g_ours.menu->setVisible( false );
	return true;
}

void HideRetail()
{
	for( int i = 0; i < kHideCount; i++ )
	{
		if( !g_ours.hidden ) g_ours.was[i] = g_ours.hide[i]->isVisible();
		g_ours.hide[i]->setVisible( false ); // every frame: Inventory messages re-show slot buttons 4..6
	}
	g_ours.hidden = true;
}

void RestoreRetail()
{
	if( !g_ours.hidden ) return;
	for( int i = 0; i < kHideCount; i++ ) g_ours.hide[i]->setVisible( g_ours.was[i] );
	g_ours.hidden = false;
}

void Close()
{
	cofinv::Reset( g_model.state );
	g_model.hover = 0;
	g_ours.grid->setVisible( false );
	g_ours.menu->setVisible( false );
}
} // namespace

void vgui::CofInventory_Frame( Panel *root )
{
	Panel *inventory = FindInventory( root );
	if( inventory != g_ours.inventory )
	{
		// a new viewport (VidInit) builds a new inventory panel; ours went with the old one
		g_ours = Ours();
		g_ours.inventory = inventory;
		if( inventory && !Bind( inventory ) && g_engine && g_engine->CofPrint )
			g_engine->CofPrint( "[cof-inv12] the client's inventory panel is not the expected one: the retail screen stays\n" );
	}
	if( !g_ours.grid ) return;

	if( Engine( COF_INV_OP_GRID, 0, nullptr, 0 ) != cofinv::kSlots )
	{
		Close();
		RestoreRetail(); // a map at six slots: the client's own screen, as it was
		return;
	}
	if( !inventory->isVisible())
	{
		Close();
		return;
	}
	HideRetail();

	g_model.visible = Engine( COF_INV_OP_THREE, 0, nullptr, 0 ) ? 3 : cofinv::kSlots;
	for( int s = 1; s <= cofinv::kSlots; s++ )
	{
		cof_vgui_inv_item_t &d = g_model.detail[s];
		cofinv::Item &it = g_model.items[s];
		it.present = Engine( COF_INV_OP_SLOT, s, &d, sizeof( d )) == 1;
		it.weapon = it.present && d.weapon;
		strncpy( it.combined, d.combined, sizeof( it.combined ) - 1 );
		it.combined[sizeof( it.combined ) - 1] = 0;
		strncpy( it.classname, d.classname, sizeof( it.classname ) - 1 );
		it.classname[sizeof( it.classname ) - 1] = 0;
	}
	cofinv::Sync( g_model.state, g_model.items, g_model.visible );

	// a press anywhere but the grid or the menu closes the menu, as the client's own does
	if( App::getInstance()->wasMousePressed( MOUSE_LEFT, nullptr ))
	{
		int x, y;
		if( !g_ours.grid->cursor( x, y ) && !( g_ours.menu->isVisible() && g_ours.menu->cursor( x, y )))
			cofinv::ClickOutside( g_model.state );
	}

	g_ours.grid->setVisible( true );
	if( g_model.state.menu )
	{
		cofinv::Menu m = cofinv::PlaceMenu( g_model.state.menu, g_model.state.weapon );
		cofinv::Rect a = cofinv::MenuArt( m );
		g_ours.menu->menu = m;
		g_ours.menu->setBounds( a.x, a.y, a.w, a.h );
		g_ours.menu->setVisible( true );
	}
	else g_ours.menu->setVisible( false );
	// added or moved after this frame's solve (App::internalTick): solve before painting
	g_ours.grid->solveTraverse();
	g_ours.menu->solveTraverse();
	g_model.blink++;
}
```

Implementation notes: if `MOUSE_LEFT` needs `#include "input.h"`, add it; if `Panel::getSurfaceBase()->createNewTextureID()` is not reachable this way, use the access path `XashSurface` exposes (`support.h:109`). `vguiapi_t` comes in through `support.h` (it includes `vgui_api.h`). No exceptions and no STL.

- [ ] **Step 4: Build.** From `ROOT`: `cmd /d /c tools\build_enhanced.cmd > evidence\inventory-grid-20261013\build-task8.log 2>&1`. Expected: exit 0, `cofinventory.cpp` compiled into `vgui.dll`. Fix warnings in the new file.
- [ ] **Step 5: Run the offline suite.** `python -m unittest tests.test_enhanced_inventory12 -v`. Expected: OK.
- [ ] **Step 6: Commit** (see Global Constraints). FreeVGUI: "Story co-op: the 12-slot inventory grid and item menu".

---

### Task 9: The grid's background art, generated from the retail images

**Files:**
- Create: `ROOT/tools/make_inventory12_art.py`
- Create: `ROOT/tests/test_inventory12_art.py`

**Interfaces:**
- Consumes (Task 7): the layout constants of `cofinventory_logic.h` (a test compares them).
- Produces: `python -m tools.make_inventory12_art <game folder>` (the folder that holds `cryoffear/`) writes `cryoffear/gfx/vgui/inventory12_left.tga` and `inventory12_left_three.tga`, and the same under `cryoffear/languages/polish/overlay/cryoffear/gfx/vgui/` when the Polish originals are there; exit 0, or `SystemExit` when there is no `640_inventory.tga`. Module API: `Image`, `read_tga`, `write_tga`, `compose( src, three )`, `slot_origin( slot )`, `generate( game ) -> list[Path]`.
- The retail art (measured 2026-10-14 on `640_inventory.tga`): 748×480, uncompressed 32-bit, bottom row first; panel texture alpha 141, box insides alpha 198; BAG text (36,35)-(85,56), POCKETS (36,195)-(151,216), ITEM DESCRIPTION (37,337)-(126,345); old boxes 99 px from (36,73) / (36,232), stride 102; description box frame (36,353)-(338,451); the strip x 342..363 is clean panel texture on every row; the left panel spans x 6..366, the right panel starts at 373.

- [ ] **Step 1: Failing test.** Create `ROOT/tests/test_inventory12_art.py`:

```python
"""The 12-slot inventory's left-half art (tools/make_inventory12_art.py)."""
import re
import tempfile
import unittest
from pathlib import Path

from tools import make_inventory12_art as art

ROOT = Path(__file__).resolve().parents[1]
LOGIC = (ROOT / 'upstream/cof-enhanced/xash3d-fwgs-4857b389e6ba32ddaa68582aedcbc950c138f46a'
         / '3rdparty/freevgui/platform/xash3d-fwgs/cofinventory_logic.h')
RETAIL = ROOT / 'runtimes/enhanced/cryoffear/gfx/vgui/640_inventory.tga'

PANEL = bytes((10, 10, 10, 141))   # BGRA, as the retail panel texture
BORDER = bytes((100, 100, 100, 198))
INSIDE = bytes((3, 3, 3, 198))
TEXT = bytes((230, 230, 230, 255))


def lum(pixel):
    return (pixel[0] + pixel[1] + pixel[2]) // 3


def synthetic(three=False):
    """A 748x480 stand-in for the retail background: panel texture, labels, six old boxes, the description box."""
    image = art.Image(748, 480, bytearray(PANEL * (748 * 480)))

    def fill(x0, y0, x1, y1, pixel):
        for y in range(y0, y1):
            o = (y * 748 + x0) * 4
            image.pixels[o:o + (x1 - x0) * 4] = pixel * (x1 - x0)

    def box(x, y, w, h):
        fill(x, y, x + w, y + h, INSIDE)
        fill(x, y, x + w, y + 1, BORDER)
        fill(x, y + h - 1, x + w, y + h, BORDER)
        fill(x, y, x + 1, y + h, BORDER)
        fill(x + w - 1, y, x + w, y + h, BORDER)

    fill(36, 35, 86, 57, TEXT)            # BAG (POCKETS in the three-slot art)
    if not three:
        fill(36, 195, 152, 217, TEXT)     # POCKETS
    fill(37, 337, 127, 346, TEXT)         # ITEM DESCRIPTION
    for top in (73, 232):
        for left in (36, 138, 240):
            box(left, top, 99, 99)
    box(36, 353, 303, 99)                 # the description box
    return image


class ArtTests(unittest.TestCase):
    def test_layout_matches_the_grid(self):
        text = LOGIC.read_text()

        def const(name):
            return int(re.search(rf'\b{name}\s*=\s*(\d+)', text).group(1))

        self.assertEqual((art.BOX, art.STRIDE, art.LEFT, art.WIDE, art.TALL),
                         (const('kBox'), const('kStride'), const('kLeft'), const('kGridWide'), const('kGridTall')))
        rows = re.search(r'kRowTop\[3\]\s*=\s*\{\s*(\d+),\s*(\d+),\s*(\d+)', text).groups()
        self.assertEqual(art.ROW_TOP, tuple(int(v) for v in rows))
        # the examine picture sits 5 px up-left of the moved description box frame (old top 353)
        self.assertEqual(353 + art.DESC_SHIFT - 5, const('kCheckY'))

    def test_compose(self):
        out = art.compose(synthetic(), three=False)
        self.assertEqual((out.wide, out.tall), (373, 480))
        self.assertEqual(lum(out.get(40, 40)), 230)          # BAG stays
        self.assertLess(lum(out.get(134, 120)), 25)          # old box 1's right border is gone (inside new box 2)
        self.assertEqual(lum(out.get(40, 232)), 230)         # POCKETS moved: text top 227
        self.assertEqual(lum(out.get(40, 342)), 230)         # ITEM DESCRIPTION moved: text top 339
        self.assertEqual(lum(out.get(100, 357)), 100)        # the description box frame, 4 px lower
        self.assertLess(lum(out.get(100, 353)), 25)          # its old top border is gone
        for slot in range(1, 13):
            x, y = art.slot_origin(slot)
            self.assertEqual(lum(out.get(x, y + 30)), 100, slot)        # left border
            self.assertEqual(lum(out.get(x + 71, y + 30)), 100, slot)   # right border
            self.assertEqual(out.get(x + 30, y + 30)[3], 198, slot)     # inside: the old inside
        self.assertEqual(out.get(370, 240), synthetic().get(370, 240))  # the gap column is untouched

    def test_compose_three(self):
        out = art.compose(synthetic(three=True), three=True)
        x, y = art.slot_origin(1)
        self.assertEqual(lum(out.get(x, y + 30)), 100)
        for slot in (4, 5, 12):                             # only boxes 1..3
            x, y = art.slot_origin(slot)
            self.assertLess(lum(out.get(x, y + 30)), 25, slot)
        self.assertLess(lum(out.get(40, 232)), 25)          # no POCKETS in the middle
        self.assertEqual(lum(out.get(40, 342)), 230)

    def test_write_read_and_generate(self):
        with tempfile.TemporaryDirectory() as tmp:
            game = Path(tmp)
            vgui = game / 'cryoffear/gfx/vgui'
            polish = game / 'cryoffear/languages/polish/overlay/cryoffear/gfx/vgui'
            art.write_tga(vgui / '640_inventory.tga', synthetic())
            art.write_tga(vgui / '640_inventory_three.tga', synthetic(three=True))
            art.write_tga(polish / '640_inventory.tga', synthetic())
            written = art.generate(game)
            self.assertEqual(sorted(p.relative_to(game).as_posix() for p in written), sorted([
                'cryoffear/gfx/vgui/inventory12_left.tga',
                'cryoffear/gfx/vgui/inventory12_left_three.tga',
                'cryoffear/languages/polish/overlay/cryoffear/gfx/vgui/inventory12_left.tga']))
            back = art.read_tga(vgui / 'inventory12_left.tga')
            self.assertEqual(back.pixels, art.compose(synthetic(), three=False).pixels)
            raw = (vgui / 'inventory12_left.tga').read_bytes()
            self.assertEqual((raw[2], raw[16], raw[17]), (2, 32, 8))  # uncompressed, 32-bit, bottom row first
            self.assertEqual(art.read_tga(vgui / '640_inventory.tga').pixels, synthetic().pixels)  # retail untouched

    def test_no_retail_art_is_an_error(self):
        with tempfile.TemporaryDirectory() as tmp:
            with self.assertRaises(SystemExit):
                art.generate(Path(tmp))

    @unittest.skipUnless(RETAIL.is_file(), 'the retail inventory art is not installed locally')
    def test_retail_art(self):
        out = art.compose(art.read_tga(RETAIL), three=False)
        self.assertEqual((out.wide, out.tall), (373, 480))
        self.assertLess(lum(out.get(134, 120)), 25)
        x, y = art.slot_origin(12)
        self.assertGreater(lum(out.get(x, y + 30)), 60)


if __name__ == '__main__':
    unittest.main()
```

- [ ] **Step 2: Run it to see it fail.** `python -m unittest tests.test_inventory12_art -v`. Expected: ERROR (`ImportError: cannot import name 'make_inventory12_art'`).

- [ ] **Step 3: The tool.** Create `ROOT/tools/make_inventory12_art.py`:

```python
"""Generate the 12-slot inventory's left-half art from the player's own retail inventory art.

Reads cryoffear/gfx/vgui/640_inventory.tga and 640_inventory_three.tga (uncompressed 32-bit, as
Cry of Fear 1.6 ships them) and writes inventory12_left.tga / inventory12_left_three.tga next to
them: the left 373x480 of the background with the six old boxes erased, the POCKETS and ITEM
DESCRIPTION labels and the description box moved, and 12 (or 3) boxes of 72 px drawn for the grid
in 3rdparty/freevgui/platform/xash3d-fwgs/cofinventory_logic.h. The Polish overlay gets the same
when its originals are present. The retail files are only read; nothing copied from the game is
stored in the repository.

    python -m tools.make_inventory12_art <game folder>     (the folder that holds cryoffear/)
"""
import argparse
import struct
import sys
from pathlib import Path

WIDE, TALL = 373, 480                 # the grid panel: the left panel and the gap after it
BOX, STRIDE, LEFT = 72, 78, 36        # cofinventory_logic.h kBox, kStride, kLeft
ROW_TOP = (68, 146, 262)              # kRowTop: BAG rows (slots 1..8), POCKETS row (9..12)
DONOR_X, DONOR_W = 342, 22            # clean panel texture on every row, right of the old boxes
ERASE = (14, 64, 366, 466)            # x0, y0, x1, y1 (end exclusive): everything below the top label
OLD_FRAME, OLD_BOX = (36, 73), 99     # the first old box, 1 px border included
LABEL_THRESHOLD = 150                 # label text is bright; the panel texture stays below 25
POCKETS_SEARCH, POCKETS_TOP = (16, 176, 360, 229), 227
ITEMDESC_SEARCH, ITEMDESC_TOP = (30, 331, 360, 351), 339
DESC_BOX, DESC_SHIFT = (33, 350, 342, 455), 4   # the description box with its frame


class Image:
    """BGRA pixels, top row first."""

    def __init__(self, wide, tall, pixels=None):
        self.wide, self.tall = wide, tall
        self.pixels = pixels if pixels is not None else bytearray(wide * tall * 4)

    def get(self, x, y):
        o = (y * self.wide + x) * 4
        return bytes(self.pixels[o:o + 4])

    def put(self, x, y, bgra):
        o = (y * self.wide + x) * 4
        self.pixels[o:o + 4] = bgra


def read_tga(path):
    data = Path(path).read_bytes()
    idlen, cmtype, itype = data[0], data[1], data[2]
    wide, tall, bpp, desc = struct.unpack_from('<HHBB', data, 12)
    if cmtype or itype != 2 or bpp != 32:
        raise ValueError(f'{path}: expected an uncompressed 32-bit TGA')
    rows = data[18 + idlen:18 + idlen + wide * tall * 4]
    if len(rows) != wide * tall * 4:
        raise ValueError(f'{path}: truncated')
    image, stride = Image(wide, tall), wide * 4
    for y in range(tall):
        source = y if desc & 0x20 else tall - 1 - y
        image.pixels[y * stride:(y + 1) * stride] = rows[source * stride:(source + 1) * stride]
    return image


def write_tga(path, image):
    stride = image.wide * 4
    out = bytearray(struct.pack('<BBBHHBHHHHBB', 0, 0, 2, 0, 0, 0, 0, 0, image.wide, image.tall, 32, 8))
    for y in range(image.tall - 1, -1, -1):   # bottom row first, as the retail files
        out += image.pixels[y * stride:(y + 1) * stride]
    Path(path).parent.mkdir(parents=True, exist_ok=True)
    Path(path).write_bytes(bytes(out))


def bright_box(image, box):
    """The bounding box (end exclusive) of the label text inside box."""
    x0, y0, x1, y1 = box
    xs, ys = [], []
    for y in range(y0, y1):
        for x in range(x0, x1):
            b, g, r, _ = image.get(x, y)
            if (r + g + b) // 3 >= LABEL_THRESHOLD:
                xs.append(x)
                ys.append(y)
    if not xs:
        raise ValueError(f'no label text in {box}')
    return min(xs), min(ys), max(xs) + 1, max(ys) + 1


def copy(src, dst, box, dx, dy):
    """The pixels of box (end exclusive) from src to dst, moved by dx, dy and clipped."""
    x0, y0, x1, y1 = box
    x0, y0 = max(x0, 0, -dx), max(y0, 0, -dy)
    x1, y1 = min(x1, src.wide, dst.wide - dx), min(y1, src.tall, dst.tall - dy)
    if x1 <= x0:
        return
    for y in range(y0, y1):
        s, d = (y * src.wide + x0) * 4, ((y + dy) * dst.wide + x0 + dx) * 4
        dst.pixels[d:d + (x1 - x0) * 4] = src.pixels[s:s + (x1 - x0) * 4]


def mirror(k, n):
    k %= 2 * n
    return k if k < n else 2 * n - 1 - k


def erase(src, dst, box):
    """Panel texture over box: the clean strip, mirrored sideways, on the same rows (keeps the vertical shading)."""
    x0, y0, x1, y1 = box
    for y in range(y0, y1):
        for x in range(x0, x1):
            dst.put(x, y, src.get(DONOR_X + mirror(x - x0, DONOR_W), y))


def frame_tile(src):
    """The old 99 px box as a 72 px one: the 1 px border kept crisp, the inside resampled."""
    fx, fy = OLD_FRAME
    border = src.get(fx, fy + OLD_BOX // 2)
    tile = Image(BOX, BOX)
    for y in range(BOX):
        for x in range(BOX):
            if x in (0, BOX - 1) or y in (0, BOX - 1):
                tile.put(x, y, border)
            else:
                tile.put(x, y, src.get(fx + 1 + (x - 1) * (OLD_BOX - 2) // (BOX - 2),
                                       fy + 1 + (y - 1) * (OLD_BOX - 2) // (BOX - 2)))
    return tile


def slot_origin(slot):
    i = slot - 1
    return LEFT + (i % 4) * STRIDE, ROW_TOP[i // 4]


def move_label(src, out, search, top, margin_x, margin_top, margin_bottom):
    x0, y0, x1, y1 = bright_box(src, search)
    copy(src, out, (max(0, x0 - margin_x), y0 - margin_top, min(DONOR_X, x1 + margin_x), y1 + margin_bottom), 0, top - y0)


def compose(src, three):
    if src.wide < WIDE or src.tall != TALL:
        raise ValueError('expected the 748x480 retail inventory background')
    out = Image(WIDE, TALL)
    copy(src, out, (0, 0, WIDE, TALL), 0, 0)
    erase(src, out, ERASE)
    if not three:
        move_label(src, out, POCKETS_SEARCH, POCKETS_TOP, 10, 9, 9)
    move_label(src, out, ITEMDESC_SEARCH, ITEMDESC_TOP, 8, 4, 4)   # 4 px: the old box border is 6 px above
    copy(src, out, DESC_BOX, 0, DESC_SHIFT)
    tile = frame_tile(src)
    for slot in range(1, (3 if three else 12) + 1):
        x, y = slot_origin(slot)
        copy(tile, out, (0, 0, BOX, BOX), x, y)
    return out


def generate(game):
    """The art for one game folder (the folder that holds cryoffear/); the files written."""
    game = Path(game)
    folders = (game / 'cryoffear/gfx/vgui', game / 'cryoffear/languages/polish/overlay/cryoffear/gfx/vgui')
    if not (folders[0] / '640_inventory.tga').is_file():
        raise SystemExit(f'{folders[0] / "640_inventory.tga"} is missing: is {game} a Cry of Fear folder?')
    written = []
    for folder in folders:
        for source, target, three in (('640_inventory.tga', 'inventory12_left.tga', False),
                                      ('640_inventory_three.tga', 'inventory12_left_three.tga', True)):
            if (folder / source).is_file():
                write_tga(folder / target, compose(read_tga(folder / source), three))
                written.append(folder / target)
    return written


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('game', help='the folder that holds cryoffear/')
    args = parser.parse_args(argv)
    for path in generate(args.game):
        print(path)
    return 0


if __name__ == '__main__':
    sys.exit(main())
```

- [ ] **Step 4: Run it to see it pass.** `python -m unittest tests.test_inventory12_art -v`. Expected: all OK (`test_retail_art` runs on this machine).
- [ ] **Step 5: Look at the real result.** `uv run --no-project python -I` a two-line script in the session scratchpad that composes the retail file and writes a PNG (stdlib `zlib` + `struct`, or Pillow if present) into `evidence/inventory-grid-20261013/inventory12_left_preview.png`; open it with the Read tool and check: 12 boxes in a 4×3 grid, BAG over the first two rows, POCKETS over the third, ITEM DESCRIPTION and its box below, no stray border lines or seams. Adjust the constants only if something is off, then rerun Step 4.
- [ ] **Step 6: Commit** (see Global Constraints). Launcher: "Generate the 12-slot inventory's background art from the retail images".

---

### Task 10: Install path for the test copies, and the live checklist

**Files:**
- Modify: `ROOT/tools/Prepare-CampaignTest.ps1`
- Modify: `ROOT/docs/campaign-test-guide.md`

**Interfaces:**
- Consumes: Task 9 `python -m tools.make_inventory12_art <copy>`; the build's `vgui.dll` (`ENG\build-local-20260927\3rdparty\freevgui\vgui.dll`).
- Produces: `Prepare-CampaignTest.ps1 -ExpectedSha256 <xash> [-ExpectedVguiSha256 <vgui>] [-Vgui <path>]`: installs `vgui.dll` beside `xash.dll` (the previous one kept as `vgui.dll.<hash8>.bak`) when `-ExpectedVguiSha256` is given, and always generates the grid art into each copy.

- [ ] **Step 1: The script.** In `tools/Prepare-CampaignTest.ps1`:
  - In the comment-based help, add after the `Installs the engine only if…` bullet: `- With -ExpectedVguiSha256, installs the matching vgui.dll (FreeVGUI; it shares vguiapi_t with xash.dll, so both come from one build).` and `- Generates the 12-slot inventory's grid art from each copy's own retail images (tools\make_inventory12_art.py).`
  - `param(...)`: add `[string]$Vgui,` and `[string]$ExpectedVguiSha256` after `[string]$Engine,`.
  - After the `$Engine` default, add:

```powershell
if (-not $Vgui) { $Vgui = Join-Path (Split-Path -Parent (Split-Path -Parent $Engine)) '3rdparty\freevgui\vgui.dll' }
```

  - After the engine hash check, add:

```powershell
$vguiHash = $null
if ($ExpectedVguiSha256) {
    $vguiHash = (Get-FileHash -LiteralPath $Vgui).Hash
    if ($vguiHash -ne $ExpectedVguiSha256.ToUpperInvariant()) { throw "vgui.dll hash $vguiHash does not match the expected build." }
}
```

  - Inside the per-role loop, right after `if ((Get-FileHash -LiteralPath $installed).Hash -ne $hash) { throw "Engine copy failed: $role" }`, add:

```powershell
    if ($vguiHash) {
        $installedVgui = Join-Path $copy 'vgui.dll'
        $oldVgui = (Get-FileHash -LiteralPath $installedVgui).Hash
        if ($oldVgui -ne $vguiHash) {
            $backup = Join-Path $copy ("vgui.dll.{0}.bak" -f $oldVgui.Substring(0, 8))
            if (-not (Test-Path -LiteralPath $backup)) { Copy-Item -LiteralPath $installedVgui -Destination $backup }
            Copy-Item -LiteralPath $Vgui -Destination $installedVgui -Force
        }
        if ((Get-FileHash -LiteralPath $installedVgui).Hash -ne $vguiHash) { throw "vgui.dll copy failed: $role" }
    }
    # The 12-slot inventory's grid art, from this copy's own retail images
    Push-Location $project
    try { & python -m tools.make_inventory12_art $copy | Out-Null } finally { Pop-Location }
    if ($LASTEXITCODE -ne 0) { throw "Inventory art generation failed: $role" }
```

  - At the end, after `"Prepared host and guest campaign copies with engine $hash."`, add `if ($vguiHash) { "vgui.dll $vguiHash installed beside it." }`.
- [ ] **Step 2: Check the script parses.** `pwsh -NoProfile -Command "$null = [System.Management.Automation.Language.Parser]::ParseFile('tools/Prepare-CampaignTest.ps1', [ref]$null, [ref]$errors); $errors"` from `ROOT`. Expected: no output. (Running it for real is Task 11.)
- [ ] **Step 3: The checklist.** Add to `docs/campaign-test-guide.md` a section:

```markdown
## 12-slot inventory (engine with the 2026-10-14 inventory build)

Story co-op only; `cof_inventory_slots 6` on the host gives the retail six. Host console: `cof_inventory12_status`, `cof_story_status` (13 `inventory-slot` lines per player).

1. `cof_inventory12_status` reads `slots=12 cvar=12 keep=1 wide=1 reason=none`; the host log has `cofstory phase=inventory12 slots=12` for each map.
2. Fill all 12 boxes (the test gun plus story items). The inventory shows a 4x3 grid: BAG over two rows, POCKETS over the third. Each pickup into slots 7..12 shows "You got the ...".
3. Equip a weapon in slot 7..12; use a key from slot 7..12 on its door; drop an item from slot 9; combine the Glock with the tactical light across slots 6 and 7; assign quick select 2 from slot 9 (its picture appears under QUICK SELECT).
4. Open an item's menu, then click the box next to it: that box's menu opens (the old slot-6 bug).
5. Level change, death and respawn, `cof_save test12` / `cof_load test12`, and an autosave from before this build: slots 7..12 are kept, the old save loads with them empty.
6. After a full clear (`c_start`), `cof_story_status` shows the morphine/syringe state unchanged (the bytes after the slot array).
7. A copy still on the previous engine joins a map at 12: refused with "This game uses the 12-slot inventory." Connected before a map loads: that map runs at six (`reason=client:<userid>`), slots 7..12 come back on the next map at 12.
8. `c_nightmare`: the inventory still does not open (the game's own rule).
```

- [ ] **Step 4: Commit** (see Global Constraints). Launcher: "Install vgui.dll and the grid art into the test copies; 12-slot live checklist".

---

### Task 11: Verify, review, install into the test copies

**Files:**
- Modify: `ROOT/AGENT_NOTES.md`
- Evidence: `ROOT/evidence/inventory-grid-20261013/` (build logs, test logs, mutation log)

- [ ] **Step 1: Full offline suite.** From `ROOT`: `python -m unittest discover -s tests > evidence\inventory-grid-20261013\full-suite.log 2>&1`. Expected: OK (156 before this work plus the new tests; skips only for files absent on the machine). Record the count.
- [ ] **Step 2: Build.** `cmd /d /c tools\build_enhanced.cmd > evidence\inventory-grid-20261013\build-final.log 2>&1`. Expected: exit 0. Record the SHA-256 of `ENG\build-local-20260927\engine\xash.dll` and `ENG\build-local-20260927\3rdparty\freevgui\vgui.dll` (`Get-FileHash`). Stage copies as `evidence\inventory-grid-20261013\xash.dll` and `vgui.dll`.
- [ ] **Step 3: Mutation check.** Write a script in the session scratchpad that, one at a time on a temporary copy of each source, applies these deliberate bugs and runs the owning test; every one must fail its test (log to `evidence\inventory-grid-20261013\mutation.log`):
  1. `sv_cof_inventory12_patch.h`: a wide site's patched bound `0x0c` → `0x0b`.
  2. `sv_cof_inventory12_patch.h`: drop one keep site.
  3. `CoF_Inv12GroupSet`: remove the rollback loop.
  4. `sv_cof_inventory12.c`: keep group removed on maps at six (`CoF_Inv12GroupSet( base, KEEP, 0, … )` on a six reason).
  5. `sv_cof_inventory12.c`: `SV_CoF_Inventory12Refuse` ignores `local`.
  6. `sv_cof_inventory12.c`: `CoF_Inv12SlotArg` uses `i <= limit + 1`.
  7. `cof_inventory12_state.h`: `CoF_Inv12Route` forwards slot 7.
  8. `cof_inventory12_state.h`: `CoF_Inv12Parse` accepts a missing terminator.
  9. `cof_inventory12_patch.h`: compare 14 of the 15 stock bytes.
  10. `cofinventory_logic.h`: `ClickSlot` keeps the old menu when another box is clicked (the slot-6 bug back).
  11. `cofinventory_logic.h`: `Sync` ignores `visible`.
  12. `make_inventory12_art.py`: `ROW_TOP` third row 262 → 258.
  13. `infostring.c`: remove the `_cofinv` important key.
  14. `sv_cof_retail.c`: `SV_CoF_RetailFreeSlots` uses `COF_RETAIL_SLOTS` instead of `SV_CoF_InventorySlots()`.
- [ ] **Step 4: Independent review.** Use superpowers:requesting-code-review on the three repositories' changes (launcher, engine, FreeVGUI) against the spec; fix Critical and Important findings test-first; list the rest in `AGENT_NOTES.md`.
- [ ] **Step 5: Install into the test copies** (disposable; no game may be running; never launch one without the user's go-ahead): `pwsh -File tools\Prepare-CampaignTest.ps1 -ExpectedSha256 <xash hash> -Engine evidence\inventory-grid-20261013\xash.dll -ExpectedVguiSha256 <vgui hash> -Vgui evidence\inventory-grid-20261013\vgui.dll`. Expected: "Prepared host and guest campaign copies…", "vgui.dll … installed beside it.", and `evidence\story-live-20261012\{host,guest}\cryoffear\gfx\vgui\inventory12_left.tga` exist.
- [ ] **Step 6: Notes.** Update `AGENT_NOTES.md` per `AGENTS.md`: what changed and where (three repositories), commands run and evidence paths, offline / build / install / live as separate states (live: not yet), the build hashes in the identity table, the next smallest live step (the checklist in `docs/campaign-test-guide.md`), and that nothing was launched.
- [ ] **Step 7: Commit** (see Global Constraints): the notes, the checklist and any review fixes.

---

## Self-review (done while writing)

- Spec coverage: section 1 server (Tasks 1–3), the two groups and mode signal (2), capability and userinfo (2, 4, 6), client store and accessor (5, 6), grid and menu (7, 8), art (9), failure table (2, 5, 6, 8), testing (every task + 11), installation (10, 11). The `CHUDControl` strip overlap and the soft scaled pictures stay live checks (spec section 6).
- Names used across tasks: `SV_CoF_InventorySlots`, `SV_CoF_RetailInThreeSlots`, `CL_CoF_Inventory12Ready`, `COM_CoF_ModuleSha256Is`, `CoF_Inv12*` (server header and client headers share the prefix but never a translation unit), `COF_INV_OP_*`, `cof_vgui_inv_item_t`, `cof_vgui_image_t`, `cofinv::*`.
- Known deviations from the spec text, already folded into it: 72 px boxes (not 75), the art drawn over the background (not swapped), the keep/wide split, `cof_inv_slots`, `_cofinv` as an important key.
