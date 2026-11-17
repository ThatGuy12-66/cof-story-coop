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

static CVAR_DEFINE_AUTO( cof_sp_melee, "1", 0, "Cry of Fear story co-op: monsters' blows, the taller's stamp and a partner's syringe are stopped by walls, as in single player; 0: the game's multiplayer test reaches any player on the line through walls" );
static CVAR_DEFINE_AUTO( cof_sp_limp, "1", 0, "Cry of Fear story co-op: below 25 health a player limps (slower, uneven steps), as in single player; 0: no limp (the game's multiplayer)" );
static CVAR_DEFINE_AUTO( cof_sp_hints, "1", 0, "Cry of Fear story co-op: the control hints (sprint, quick slots, saving, dodge, a struggle's prompt...), once per player, as in single player; 0: none (the game's multiplayer)" );
static CVAR_DEFINE_AUTO( cof_sp_morphine_trip, "1", 0, "Cry of Fear story co-op: a second syringe too soon after the last starts the morphine trip, as in single player; 0: never (the game's multiplayer)" );
static CVAR_DEFINE_AUTO( cof_sp_details, "1", 0, "Cry of Fear story co-op: single-player details (monsters hear a hard landing, the Glock's brass, the intro card's language, a respawn clears only its own message strip, no stray Simon health bar in the last ending); 0: the game's multiplayer" );

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
	case COF_GATE_DEATH_NOTICE: return true;
	case COF_GATE_LIMP_ON:
	case COF_GATE_LIMP_OFF:
	case COF_GATE_LIMP_RESTORE: return cof_sp_limp.value != 0.0f;
	case COF_GATE_HINTS: return cof_sp_hints.value != 0.0f;
	case COF_GATE_MORPHINE_TRIP: return cof_sp_morphine_trip.value != 0.0f;
	case COF_GATE_LANDING_SOUND:
	case COF_GATE_SPAWN_STRIP:
	case COF_GATE_GLOCK_BRASS:
	case COF_GATE_INTRO_LANGUAGE:
	case COF_GATE_BOOKSIMON_BOSS: return cof_sp_details.value != 0.0f;
	}
	return false;
}

// Monster melee (cof_sp_melee). CBaseMonster::CheckTraceHullAttack (the shared claw/bite/swing helper, combat.cpp:1556
// in the decompilation), the taller's stamp and the partner heal trace a hull, which walls stop, and then in
// multiplayer also trace every living player's model alone with pfnTraceModel, which no wall stops: blows landed
// through walls and doors, and a stamp killed a player behind one. That loop is how a monster reaches a player who
// touches another (the game's multiplayer PreThink makes them SOLID_NOT, player.cpp:4072-4089, which the hull trace
// skips), so it stays; pfnTraceModel on a player (sv_game.c) misses when a MOVE_NOMONSTERS trace with the same hull
// meets the world or a brush entity first.
qboolean SV_CoF_MeleeWalls( void )
{
	return cof_sp_melee.value != 0.0f && SV_CoF_StoryCoopEnabled();
}

qboolean SV_CoF_FriendlyFire( void )
{
	return cof_coop_friendly_fire.value != 0.0f;
}

void SV_CoF_GatesInit( void )
{
	Cvar_RegisterVariable( &cof_coop_friendly_fire );
	Cvar_RegisterVariable( &cof_sp_carry );
	Cvar_RegisterVariable( &cof_sp_melee );
	Cvar_RegisterVariable( &cof_sp_limp );
	Cvar_RegisterVariable( &cof_sp_hints );
	Cvar_RegisterVariable( &cof_sp_morphine_trip );
	Cvar_RegisterVariable( &cof_sp_details );
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
