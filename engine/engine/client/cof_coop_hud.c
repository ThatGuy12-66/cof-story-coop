/*
cof_coop_hud.c - Cry of Fear story co-op: client.dll's single-player assumptions in a multiplayer game (cof_coop_hud.h)
GPL-3.0-or-later.

With the retail 1.6 client.dll, in a multiplayer game: the HUD's map-name test is skipped while cof_coop_hud is 1,
the tape recorder leaves the single-player save labels alone while cof_coop_sp_save_labels is 1, and the first-person
arms are Simon's while cof_coop_simon_arms is 1 (all the default; not the arms on maps named cof_); each goes back to
client.dll's own bytes otherwise. Log: [cof-hud] name on|off (once per change).
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
static CVAR_DEFINE_AUTO( cof_coop_simon_arms, "1", FCVAR_ARCHIVE, "Cry of Fear story co-op: the first-person arms are Simon's (or the costume's), as in single player; 0: client.dll's police officer's sleeves and hands in any multiplayer game" );
static CVAR_DEFINE_AUTO( cof_coop_sp_save_labels, "1", FCVAR_ARCHIVE, "Cry of Fear story co-op: a co-op tape save leaves this machine's single-player save slot labels (SAVE/saveinfoN.cof) alone; 0: client.dll overwrites them, as it does" );

static convar_t *const cof_hud_cvars[COF_CLIENT_GATES] = { &cof_coop_hud, &cof_coop_sp_save_labels, &cof_coop_simon_arms };
static byte *cof_hud_base; // the retail client.dll's image, or NULL
static int cof_hud_state[COF_CLIENT_GATES]; // -1 unknown, 0 client.dll's bytes, 1 ours

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

static void CL_CoF_CoopHudForget( void )
{
	int i;
	cof_hud_base = NULL;
	for( i = 0; i < COF_CLIENT_GATES; i++ ) cof_hud_state[i] = -1;
}

void CL_CoF_CoopHudInit( void )
{
	Cvar_RegisterVariable( &cof_coop_hud );
	Cvar_RegisterVariable( &cof_coop_sp_save_labels );
	Cvar_RegisterVariable( &cof_coop_simon_arms );
	CL_CoF_CoopHudForget();
}

// cl_game.c CL_LoadProgs: after the module is loaded
void CL_CoF_CoopHudLoad( void *module )
{
#if COF_HUD_PATCH
	dll_user_t *lib = (dll_user_t *)module;
	CL_CoF_CoopHudForget();
	if( !lib || !lib->hInstance || lib->custom_loader || !COM_CoF_ModuleSha256Is( lib, COF_HUD_CLIENT_SHA256 )) return;
	cof_hud_base = (byte *)lib->hInstance;
#else
	(void)module;
#endif
}

// cl_game.c CL_UnloadProgs: before the module is freed (its bytes go with it)
void CL_CoF_CoopHudUnload( void )
{
	CL_CoF_CoopHudForget();
}

void CL_CoF_CoopHudFrame( void )
{
#if COF_HUD_PATCH
	int i, on;
	for( i = 0; cof_hud_base && i < COF_CLIENT_GATES; i++ )
	{
		on = cls.state >= ca_connected && CoF_ClientGateWanted( i, cof_hud_cvars[i]->value != 0.0f, cl.maxclients, clgame.mapname );
		if( on == cof_hud_state[i] ) continue;
		if( !CoF_GateSet( cof_hud_base, cof_client_gates[i], on, CL_CoF_CoopHudWrite ))
		{
			Con_Printf( S_WARN "[cof-hud] client.dll has unexpected bytes at %s: left as it is\n", cof_client_gates[i]->name );
			cof_hud_state[i] = on; // not tried again
			continue;
		}
		cof_hud_state[i] = on;
		Con_Printf( "[cof-hud] %s %s\n", cof_client_gates[i]->name, on ? "on" : "off" );
	}
#endif
}
