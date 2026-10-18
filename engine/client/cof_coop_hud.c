/*
cof_coop_hud.c - Cry of Fear story co-op: client.dll's co-op HUD on the story maps (cof_coop_hud.h)
GPL-3.0-or-later.

With the retail 1.6 client.dll, in a multiplayer game and while cof_coop_hud is 1 (the default), the HUD's map-name
test is skipped; it goes back to client.dll's own bytes otherwise. Log: [cof-hud] on|off (once per change).
*/
#include "common.h"
#include "client.h"
#include "library.h"
#include "sv_cof_gates.h"
#include "cof_coop_hud.h"
// Code patching only where the rest of the story co-op does it (sv_cof_story.c)
#if XASH_WIN32 && XASH_X86 && defined( XASH_COF_ENTVARS_LEGACY ) && XASH_COF_ENTVARS_LEGACY
#define COF_HUD_PATCH 1
#include <windows.h>
#else
#define COF_HUD_PATCH 0
#endif

#define COF_HUD_CLIENT_SHA256 "d2a04641b301804f6f449aa68265042b13adc360925b80033d417ec9f38c9c00" // client.dll, Cry of Fear 1.6

static CVAR_DEFINE_AUTO( cof_coop_hud, "1", FCVAR_ARCHIVE, "Cry of Fear story co-op: chat and the co-op HUD (player list, deaths, who you look at) on the story maps; client.dll shows them only on maps named cof_" );

static byte *cof_hud_base; // the retail client.dll's image, or NULL
static int cof_hud_state = -1; // -1 unknown, 0 client.dll's bytes, 1 ours

#if COF_HUD_PATCH
static int CL_CoF_CoopHudWrite( byte *p, const byte *value, int size )
{
	DWORD protect, ignored;
	if( !VirtualProtect( p, size, PAGE_EXECUTE_READWRITE, &protect )) return 0;
	memcpy( p, value, size );
	FlushInstructionCache( GetCurrentProcess(), p, size );
	VirtualProtect( p, size, protect, &ignored );
	return 1;
}
#endif

void CL_CoF_CoopHudInit( void )
{
	Cvar_RegisterVariable( &cof_coop_hud );
}

// cl_game.c CL_LoadProgs: after the module is loaded
void CL_CoF_CoopHudLoad( void *module )
{
#if COF_HUD_PATCH
	dll_user_t *lib = (dll_user_t *)module;
	cof_hud_base = NULL;
	cof_hud_state = -1;
	if( !lib || !lib->hInstance || lib->custom_loader || !COM_CoF_ModuleSha256Is( lib, COF_HUD_CLIENT_SHA256 )) return;
	cof_hud_base = (byte *)lib->hInstance;
#else
	(void)module;
#endif
}

// cl_game.c CL_UnloadProgs: before the module is freed (its bytes go with it)
void CL_CoF_CoopHudUnload( void )
{
	cof_hud_base = NULL;
	cof_hud_state = -1;
}

void CL_CoF_CoopHudFrame( void )
{
#if COF_HUD_PATCH
	int on;
	if( !cof_hud_base ) return;
	on = cls.state >= ca_connected && CoF_CoopHudWanted( cof_coop_hud.value != 0.0f, cl.maxclients );
	if( on == cof_hud_state ) return;
	if( !CoF_GateSet( cof_hud_base, &cof_coop_hud_gate, on, CL_CoF_CoopHudWrite ))
	{
		Con_Printf( S_WARN "[cof-hud] client.dll's HUD has unexpected bytes: chat stays hidden on the story maps\n" );
		cof_hud_base = NULL;
		return;
	}
	cof_hud_state = on;
	Con_Printf( "[cof-hud] co-op HUD on the story maps: %s\n", on ? "on" : "off" );
#endif
}
