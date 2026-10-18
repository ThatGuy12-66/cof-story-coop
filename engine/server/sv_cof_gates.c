/*
sv_cof_gates.c - Cry of Fear story co-op: small, verified changes to the retail hl.dll (sv_cof_gates.h)
GPL-3.0-or-later.

Log: phase=gate name on|off (once per change), phase=gate-refused name (bytes of another build).
*/
#include "common.h"
#include "server.h"
#include "library.h"
#include "sv_cof_story.h"
#include "sv_cof_gates.h"
#if XASH_WIN32
#include <windows.h>
#endif

static CVAR_DEFINE_AUTO( cof_coop_friendly_fire, "0", 0, "Cry of Fear story co-op: players' shots and blows hurt other players, the AI companions included (0: nobody's, as the game does for humans)" );

static CVAR_DEFINE_AUTO( cof_sp_carry, "1", 0, "Cry of Fear story co-op: the single-player carry limits (10 syringes, flares, full ammo); 0: the game's multiplayer limits (one syringe, a third of the ammo, no flares)" );

static byte *cof_gates_base; // where the gates were written, and for which DLL instance
static void *cof_gates_instance;
static signed char cof_gates_state[COF_GATES]; // -1 unknown, 0 retail, 1 ours
static qboolean cof_gates_refused[COF_GATES];

static int CoF_GatesWrite( byte *p, const byte *value, int size )
{
#if XASH_WIN32 && XASH_X86
	DWORD protect, ignored;
	if( !VirtualProtect( p, size, PAGE_EXECUTE_READWRITE, &protect )) return 0;
	memcpy( p, value, size );
	FlushInstructionCache( GetCurrentProcess(), p, size );
	VirtualProtect( p, size, protect, &ignored );
	return 1;
#else
	return 0;
#endif
}

static qboolean CoF_GateWanted( int gate )
{
	switch( gate )
	{
	case COF_GATE_FRIENDLY_FIRE: return cof_coop_friendly_fire.value == 0.0f;
	case COF_GATE_CARRY_MAX_AMMO:
	case COF_GATE_CARRY_AMMO_G43:
	case COF_GATE_CARRY_AMMO_GLOCK:
	case COF_GATE_CARRY_AMMO_M16:
	case COF_GATE_CARRY_AMMO_MP5:
	case COF_GATE_CARRY_AMMO_P345:
	case COF_GATE_CARRY_AMMO_REVOLVER:
	case COF_GATE_CARRY_AMMO_RIFLE:
	case COF_GATE_CARRY_AMMO_SHELLS:
	case COF_GATE_CARRY_AMMO_TMP:
	case COF_GATE_CARRY_AMMO_VP70: return cof_sp_carry.value != 0.0f;
	case COF_GATE_RAIN_MODIFY: return SV_CoF_WeatherOn();
	}
	return false;
}

qboolean SV_CoF_FriendlyFire( void )
{
	return cof_coop_friendly_fire.value != 0.0f;
}

void SV_CoF_GatesInit( void )
{
	Cvar_RegisterVariable( &cof_coop_friendly_fire );
	Cvar_RegisterVariable( &cof_sp_carry );
	memset( cof_gates_state, -1, sizeof( cof_gates_state ));
}

void SV_CoF_GatesClose( void )
{
	int i;
	if( cof_gates_base && cof_gates_instance == svgame.hInstance )
		for( i = 0; i < COF_GATES; i++ )
			if( cof_gates_state[i] == 1 ) CoF_GateSet( cof_gates_base, &cof_gates[i], 0, CoF_GatesWrite );
	cof_gates_base = NULL;
	cof_gates_instance = NULL;
	memset( cof_gates_state, -1, sizeof( cof_gates_state ));
	memset( cof_gates_refused, 0, sizeof( cof_gates_refused ));
}

void SV_CoF_GatesFrame( void )
{
	byte *base = (byte *)SV_CoF_StoryBase();
	int i;
	if( !base ) return;
	if( base != cof_gates_base || cof_gates_instance != svgame.hInstance )
	{
		cof_gates_base = base;
		cof_gates_instance = svgame.hInstance;
		memset( cof_gates_state, -1, sizeof( cof_gates_state ));
		memset( cof_gates_refused, 0, sizeof( cof_gates_refused ));
	}
	for( i = 0; i < COF_GATES; i++ )
	{
		int on = CoF_GateWanted( i );
		if( cof_gates_state[i] == on || cof_gates_refused[i] ) continue;
		if( !CoF_GateSet( base, &cof_gates[i], on, CoF_GatesWrite ))
		{
			cof_gates_refused[i] = true;
			Con_Printf( "cofstory phase=gate-refused name=%s\n", cof_gates[i].name );
			continue;
		}
		cof_gates_state[i] = (signed char)on;
		Con_Printf( "cofstory phase=gate name=%s %s\n", cof_gates[i].name, on ? "on" : "off" );
	}
}
