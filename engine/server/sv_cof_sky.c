/*
sv_cof_sky.c - Cry of Fear story co-op: a merged map's 3D skies, each section its own (sv_cof_sky.h)
GPL-3.0-or-later.

With cof_coop_section_sky (default 1), in a merged map: the game's sky markers to a player are kept, by section, and
held back; a player who joins, respawns or moves to another section gets their section's markers at once; and the
sky models of the other sections are not sent to them. Log: phase=sky-sent userid section markers.
*/
#include "common.h"
#include "server.h"
#include "cof_sections.h"
#include "sv_cof_story.h"
#include "sv_cof_sections.h"
#include "sv_cof_sky.h"

#define COF_SKY_UNKNOWN -2

static CVAR_DEFINE_AUTO( cof_coop_section_sky, "1", 0, "Cry of Fear story co-op: each section of a merged map shows its own 3D sky; 0: every section the last member's, as the game does" );

static struct { int size; byte data[COF_SKY_PAYLOAD_MAX]; } cof_sky_marks[COF_SECT_MAX][2];
static struct { int userid; qboolean replay; int section; } cof_sky_players[MAX_CLIENTS];
static int cof_sky_spawncount = -1;
static qboolean cof_sky_sending;

void SV_CoF_SkyInit( void )
{
	Cvar_RegisterVariable( &cof_coop_section_sky );
}

void SV_CoF_SkyReset( void )
{
	memset( cof_sky_marks, 0, sizeof( cof_sky_marks ));
	memset( cof_sky_players, 0, sizeof( cof_sky_players ));
	cof_sky_spawncount = -1;
	cof_sky_sending = false;
}

static qboolean CoF_SkyOn( void )
{
	if( cof_coop_section_sky.value == 0.0f || !SV_CoF_StoryCoopEnabled( )) return false;
	if( cof_sky_spawncount != svs.spawncount ) // the markers are the loaded map's
	{
		memset( cof_sky_marks, 0, sizeof( cof_sky_marks ));
		cof_sky_spawncount = svs.spawncount;
	}
	return true;
}

static int CoF_SkySlot( const edict_t *to )
{
	sv_client_t *cl = SV_ClientFromEdict( to, true );
	int slot = cl ? (int)( cl - svs.clients ) : -1;
	if( !cl || FBitSet( cl->flags, FCL_FAKECLIENT ) || slot < 0 || slot >= MAX_CLIENTS ) return -1;
	if( cof_sky_players[slot].userid != cl->userid )
	{
		cof_sky_players[slot].userid = cl->userid;
		cof_sky_players[slot].replay = false;
		cof_sky_players[slot].section = COF_SKY_UNKNOWN;
	}
	return slot;
}

qboolean SV_CoF_SkyMessage( edict_t *to, int dest, const char *name, const byte *payload, int size )
{
	vec3_t point = { 0.0f, 0.0f, 0.0f };
	int kind = CoF_SkyMarker( name ), section, slot;
	if( cof_sky_sending || kind < 0 || !to || ( dest != MSG_ONE && dest != MSG_ONE_UNRELIABLE ) || size > COF_SKY_PAYLOAD_MAX ) return false;
	if( !CoF_SkyOn() || SV_CoF_PlayerSection( to ) < 0 ) return false;
	if( !CoF_SkyMarkerHeight( payload, size, FBitSet( host.features, ENGINE_WRITE_LARGE_COORD ) != 0, &point[2] )) return false;
	if(( section = SV_CoF_PointSection( point )) < 0 || section >= COF_SECT_MAX || ( slot = CoF_SkySlot( to )) < 0 ) return false;
	cof_sky_marks[section][kind].size = size;
	memcpy( cof_sky_marks[section][kind].data, payload, size );
	cof_sky_players[slot].replay = true;
	return true;
}

qboolean SV_CoF_SkyHiddenFor( const edict_t *viewer, const edict_t *ent, int renderfx )
{
	vec3_t center;
	if( !CoF_SkyModel( renderfx ) || cof_coop_section_sky.value == 0.0f || !viewer || !ent ) return false;
	VectorAverage( ent->v.absmin, ent->v.absmax, center );
	return CoF_SkyHidden( renderfx, SV_CoF_PlayerSection( viewer ), SV_CoF_PointSection( center ));
}

void SV_CoF_SkyFrame( void )
{
	int i;
	if( !CoF_SkyOn( )) return;
	for( i = 0; i < svs.maxclients && i < MAX_CLIENTS; i++ )
	{
		sv_client_t *cl = &svs.clients[i];
		int slot, section, kind, sent = 0;
		qboolean moved;
		if( cl->state != cs_spawned || !SV_IsValidEdict( cl->edict ) || ( slot = CoF_SkySlot( cl->edict )) < 0 ) continue;
		if(( section = SV_CoF_PlayerSection( cl->edict )) < 0 || section >= COF_SECT_MAX ) continue;
		moved = cof_sky_players[slot].section != COF_SKY_UNKNOWN && section != cof_sky_players[slot].section;
		cof_sky_players[slot].section = section;
		if( !cof_sky_players[slot].replay && !moved ) continue;
		cof_sky_players[slot].replay = false;
		cof_sky_sending = true;
		for( kind = 0; kind < 2; kind++ )
		{
			if( !cof_sky_marks[section][kind].size ) continue;
			SV_CoF_SendUserMessage( cl->edict, kind ? "skymark_w" : "skymark_sky", cof_sky_marks[section][kind].data, cof_sky_marks[section][kind].size, true );
			sent++;
		}
		cof_sky_sending = false;
		Con_Printf( "cofstory phase=sky-sent userid=%d section=%d markers=%d\n", cl->userid, section, sent );
	}
}
