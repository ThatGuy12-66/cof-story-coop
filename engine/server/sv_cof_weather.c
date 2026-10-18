/*
sv_cof_weather.c - Cry of Fear story co-op: scripted weather for every player (sv_cof_weather.h)
GPL-3.0-or-later.

With cof_coop_weather (default 1) the rain-modify gate (sv_cof_gates.h) lets a named rain_modify change player 1's rain
as in single player, and every frame each other spawned player is given player 1's when it heads for different rain.
Log: phase=rain-mirror userid drips (once per copy).
*/
#include "common.h"
#include "server.h"
#include "sv_cof_story.h"
#include "sv_cof_cheats.h"
#include "sv_cof_weather.h"

static CVAR_DEFINE_AUTO( cof_coop_weather, "1", 0, "Cry of Fear story co-op: the story's scripted rain and snow (a named rain_modify) for every player; 0: only the map's constant rain, as the game does in multiplayer" );

void SV_CoF_WeatherInit( void )
{
	Cvar_RegisterVariable( &cof_coop_weather );
}

qboolean SV_CoF_WeatherOn( void )
{
	return cof_coop_weather.value != 0.0f;
}

void SV_CoF_WeatherFrame( void )
{
	edict_t *first;
	byte *host;
	int i;
	if( !SV_CoF_WeatherOn() || svs.maxclients < 2 ) return;
	first = svs.clients[0].edict; // edict 1, the one the game's rain_modify writes
	if( svs.clients[0].state != cs_spawned || !SV_IsValidEdict( first )) return;
	if( !( host = SV_CoF_StoryEntityPrivate( first, COF_RAIN_PLAYER_NEED ))) return;
	for( i = 1; i < svs.maxclients && i < MAX_CLIENTS; i++ )
	{
		sv_client_t *cl = &svs.clients[i];
		byte *player;
		if( cl->state != cs_spawned || !SV_IsValidEdict( cl->edict )) continue;
		if( !( player = SV_CoF_StoryEntityPrivate( cl->edict, COF_RAIN_PLAYER_NEED ))) continue;
		if( CoF_RainMirror( host, player ))
			Con_Printf( "cofstory phase=rain-mirror userid=%d drips=%d\n", cl->userid, *(int *)( host + COF_RAIN_BLOCK ));
	}
}
