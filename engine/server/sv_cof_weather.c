/*
sv_cof_weather.c - Cry of Fear story co-op: scripted weather for every player, and per section in merged maps
(sv_cof_weather.h)
GPL-3.0-or-later.

With cof_coop_weather (default 1) the rain-modify gate (sv_cof_gates.h) lets a named rain_modify change player 1's rain
as in single player, and every frame each other spawned player in player 1's section (a merged map) or anywhere is
given player 1's when it heads for different rain.
In a merged map the game's RainData (the map's first rain_settings and constant rain_modify, for everyone) is held back,
and a player who respawns, joins or moves to another section gets their section's rain at once (sv_cof_weather.h).
Log: phase=rain-mirror userid drips (once per copy), phase=rain-sent userid section drips.
*/
#include "common.h"
#include "server.h"
#include "sv_cof_story.h"
#include "sv_cof_cheats.h"
#include "sv_cof_sections.h"
#include "sv_cof_weather.h"

#define COF_RAIN_SETTINGS_NEED 0xE4 // CRainSettings: float Rain_Distance +0xDC, int Rain_Mode +0xE0 (PostThink 100F8133)
#define COF_RAIN_MODIFY_NEED   0xF4 // CRainModify: int Rain_Drips +0xDC, wind x, y, rand x, y (Use 100A9A0C)
#define COF_RAIN_MAX 32
#define COF_RAIN_UNKNOWN -2

static CVAR_DEFINE_AUTO( cof_coop_weather, "1", 0, "Cry of Fear story co-op: the story's scripted rain and snow (a named rain_modify) for every player, and each merged map section's own rain; 0: only the map's constant rain, as the game does in multiplayer" );

static struct { int userid; qboolean replay; int section; } cof_rain_players[MAX_CLIENTS];
static qboolean cof_rain_sending;

void SV_CoF_WeatherInit( void )
{
	Cvar_RegisterVariable( &cof_coop_weather );
}

qboolean SV_CoF_WeatherOn( void )
{
	return cof_coop_weather.value != 0.0f;
}

void SV_CoF_WeatherReset( void )
{
	memset( cof_rain_players, 0, sizeof( cof_rain_players ));
	cof_rain_sending = false;
}

static int CoF_RainSlot( const edict_t *to )
{
	sv_client_t *cl = SV_ClientFromEdict( to, true );
	int slot = cl ? (int)( cl - svs.clients ) : -1;
	if( !cl || FBitSet( cl->flags, FCL_FAKECLIENT ) || slot < 0 || slot >= MAX_CLIENTS ) return -1;
	if( cof_rain_players[slot].userid != cl->userid )
	{
		cof_rain_players[slot].userid = cl->userid;
		cof_rain_players[slot].replay = false;
		cof_rain_players[slot].section = COF_RAIN_UNKNOWN;
	}
	return slot;
}

// Every rain_settings and constant rain_modify of the map, with its section.
static void CoF_RainGather( cof_rain_settings_t *settings, int *nsettings, cof_rain_constant_t *constants, int *nconstants )
{
	int i;
	*nsettings = *nconstants = 0;
	for( i = svs.maxclients + 1; i < svgame.numEntities; i++ )
	{
		edict_t *ent = SV_EdictNum( i );
		const char *classname;
		byte *pv;
		if( !SV_IsValidEdict( ent ) || FBitSet( ent->v.flags, FL_KILLME )) continue;
		classname = SV_GetString( ent->v.classname );
		if( !Q_strcmp( classname, "rain_settings" ) && *nsettings < COF_RAIN_MAX && ( pv = SV_CoF_StoryEntityPrivate( ent, COF_RAIN_SETTINGS_NEED )))
		{
			cof_rain_settings_t *s = &settings[( *nsettings )++];
			s->section = SV_CoF_PointSection( ent->v.origin );
			memcpy( &s->distance, pv + 0xDC, sizeof( s->distance ));
			memcpy( &s->mode, pv + 0xE0, sizeof( s->mode ));
			s->height = ent->v.origin[2];
		}
		else if( !Q_strcmp( classname, "rain_modify" ) && FBitSet( ent->v.spawnflags, 1 ) && *nconstants < COF_RAIN_MAX &&
			( pv = SV_CoF_StoryEntityPrivate( ent, COF_RAIN_MODIFY_NEED )))
		{
			cof_rain_constant_t *c = &constants[( *nconstants )++];
			c->section = SV_CoF_PointSection( ent->v.origin );
			memcpy( &c->drips, pv + 0xDC, sizeof( c->drips ));
			memcpy( &c->wind_x, pv + 0xE0, sizeof( c->wind_x ));
			memcpy( &c->wind_y, pv + 0xE4, sizeof( c->wind_y ));
			memcpy( &c->rand_x, pv + 0xE8, sizeof( c->rand_x ));
			memcpy( &c->rand_y, pv + 0xEC, sizeof( c->rand_y ));
		}
	}
}

qboolean SV_CoF_RainMessage( edict_t *to, int dest, const char *name, const byte *payload, int size )
{
	int slot;
	if( cof_rain_sending || !name || !to || Q_strcmp( name, "RainData" ) || ( dest != MSG_ONE && dest != MSG_ONE_UNRELIABLE )) return false;
	if( !SV_CoF_WeatherOn() || !SV_CoF_StoryCoopEnabled() || SV_CoF_PlayerSection( to ) < 0 ) return false;
	if(( slot = CoF_RainSlot( to )) < 0 ) return false;
	cof_rain_players[slot].replay = true; // the section's own, this frame
	return true;
}

// A merged map: the section's rain for players who respawned, joined or moved to another section.
static void CoF_RainSections( void )
{
	cof_rain_settings_t settings[COF_RAIN_MAX];
	cof_rain_constant_t constants[COF_RAIN_MAX];
	int i, nsettings = -1, nconstants = 0;
	for( i = 0; i < svs.maxclients && i < MAX_CLIENTS; i++ )
	{
		sv_client_t *cl = &svs.clients[i];
		cof_rain_msg_t rain;
		cof_rain_t own;
		byte payload[COF_RAIN_PAYLOAD], *pv;
		int slot, section;
		qboolean moved;
		if( cl->state != cs_spawned || !SV_IsValidEdict( cl->edict ) || ( slot = CoF_RainSlot( cl->edict )) < 0 ) continue;
		if(( section = SV_CoF_PlayerSection( cl->edict )) < 0 ) continue;
		moved = cof_rain_players[slot].section != COF_RAIN_UNKNOWN && section != cof_rain_players[slot].section;
		cof_rain_players[slot].section = section;
		if( !cof_rain_players[slot].replay && !moved ) continue;
		cof_rain_players[slot].replay = false;
		if( !( pv = SV_CoF_StoryEntityPrivate( cl->edict, COF_RAIN_PLAYER_NEED ))) continue;
		if( nsettings < 0 ) CoF_RainGather( settings, &nsettings, constants, &nconstants );
		memcpy( &own, pv + COF_RAIN_BLOCK, sizeof( own ));
		CoF_RainForSection( settings, nsettings, constants, nconstants, section, &own, &rain );
		CoF_RainPayload( &rain, FBitSet( host.features, ENGINE_WRITE_LARGE_COORD ) != 0, payload );
		cof_rain_sending = true;
		SV_CoF_SendUserMessage( cl->edict, "RainData", payload, sizeof( payload ), true );
		cof_rain_sending = false;
		Con_Printf( "cofstory phase=rain-sent userid=%d section=%d drips=%d\n", cl->userid, section, rain.drips );
	}
}

void SV_CoF_WeatherFrame( void )
{
	edict_t *first;
	byte *host;
	int i, host_section;
	if( !SV_CoF_WeatherOn() || svs.maxclients < 2 ) return;
	first = svs.clients[0].edict; // edict 1, the one the game's rain_modify writes
	if( svs.clients[0].state == cs_spawned && SV_IsValidEdict( first ) && ( host = SV_CoF_StoryEntityPrivate( first, COF_RAIN_PLAYER_NEED )))
	{
		host_section = SV_CoF_PlayerSection( first );
		for( i = 1; i < svs.maxclients && i < MAX_CLIENTS; i++ )
		{
			sv_client_t *cl = &svs.clients[i];
			byte *player;
			if( cl->state != cs_spawned || !SV_IsValidEdict( cl->edict )) continue;
			if( SV_CoF_PlayerSection( cl->edict ) != host_section ) continue; // another section's rain is its own
			if( !( player = SV_CoF_StoryEntityPrivate( cl->edict, COF_RAIN_PLAYER_NEED ))) continue;
			if( CoF_RainMirror( host, player ))
				Con_Printf( "cofstory phase=rain-mirror userid=%d drips=%d\n", cl->userid, *(int *)( host + COF_RAIN_BLOCK ));
		}
	}
	CoF_RainSections();
}
