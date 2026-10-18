/*
sv_cof_fog.c - Cry of Fear story co-op: fog for every player, and per section in merged maps (sv_cof_fog.h)
GPL-3.0-or-later.

- After the game's ResetHUD or InitHUD to a player (a respawn, a join: the client clears its fog), the fog they
  should have is sent again, at once.
- In a merged map, a game SetFog that is not of the receiver's section's fogs is held back, and a player moving
  to another section gets that section's fog (or none) at once.
Log: phase=fog-held userid (a section's fog kept from another), phase=fog-sent userid reason=reset|section.
*/
#include "common.h"
#include "server.h"
#include "sv_cof_story.h"
#include "sv_cof_cheats.h"
#include "sv_cof_sections.h"
#include "sv_cof_fog.h"

#define COF_FOG_SIZE 0xF4
#define COF_FOG_START 0xDC // int m_iStartDist
#define COF_FOG_END 0xE0 // int m_iEndDist
#define COF_FOG_MAX 32
#define COF_SF_FOG_ACTIVE 1
#define COF_SF_FOG_FADING 0x8000
#define COF_FOG_UNKNOWN -2

static struct { int userid; qboolean replay; int section; } cof_fog_players[MAX_CLIENTS];
static qboolean cof_fog_sending;

static int CoF_FogGather( cof_fog_t *out, int max )
{
	int i, count = 0;
	for( i = svs.maxclients + 1; i < svgame.numEntities && count < max; i++ )
	{
		edict_t *ent = SV_EdictNum( i );
		byte *pv;
		if( !SV_IsValidEdict( ent ) || FBitSet( ent->v.flags, FL_KILLME ) || Q_strcmp( SV_GetString( ent->v.classname ), "env_fog" )) continue;
		if( !( pv = SV_CoF_StoryEntityPrivate( ent, COF_FOG_SIZE ))) continue;
		out[count].section = SV_CoF_PointSection( ent->v.origin );
		out[count].active = FBitSet( ent->v.spawnflags, COF_SF_FOG_ACTIVE ) != 0;
		out[count].fading = FBitSet( ent->v.spawnflags, COF_SF_FOG_FADING ) != 0;
		VectorCopy( ent->v.rendercolor, out[count].color );
		memcpy( &out[count].start, pv + COF_FOG_START, sizeof( int ));
		memcpy( &out[count].end, pv + COF_FOG_END, sizeof( int ));
		count++;
	}
	return count;
}

static int CoF_FogSlot( const edict_t *to )
{
	sv_client_t *cl = SV_ClientFromEdict( to, true );
	int slot = cl ? (int)( cl - svs.clients ) : -1;
	if( !cl || FBitSet( cl->flags, FCL_FAKECLIENT ) || slot < 0 || slot >= MAX_CLIENTS ) return -1;
	if( cof_fog_players[slot].userid != cl->userid )
	{
		cof_fog_players[slot].userid = cl->userid;
		cof_fog_players[slot].replay = false;
		cof_fog_players[slot].section = COF_FOG_UNKNOWN;
	}
	return slot;
}

qboolean SV_CoF_FogMessage( edict_t *to, int dest, const char *name, const byte *payload, int size )
{
	cof_fog_t fogs[COF_FOG_MAX];
	int slot, section, count;
	if( cof_fog_sending || !name || !to || !SV_CoF_StoryCoopEnabled() ) return false;
	if( !Q_strcmp( name, "ResetHUD" ) || !Q_strcmp( name, "InitHUD" ))
	{
		if(( slot = CoF_FogSlot( to )) >= 0 ) cof_fog_players[slot].replay = true;
		return false;
	}
	if( Q_strcmp( name, "SetFog" ) || size != COF_FOG_PAYLOAD || !payload || ( dest != MSG_ONE && dest != MSG_ONE_UNRELIABLE )) return false;
	if(( slot = CoF_FogSlot( to )) < 0 || ( section = SV_CoF_PlayerSection( to )) < 0 ) return false;
	count = CoF_FogGather( fogs, COF_FOG_MAX );
	if( CoF_FogFits( fogs, count, section, payload )) return false;
	Con_Printf( "cofstory phase=fog-held userid=%d section=%d\n", cof_fog_players[slot].userid, section );
	return true;
}

void SV_CoF_FogFrame( void )
{
	cof_fog_t fogs[COF_FOG_MAX];
	int i, count = -1;
	for( i = 0; i < svs.maxclients && i < MAX_CLIENTS; i++ )
	{
		sv_client_t *cl = &svs.clients[i];
		byte payload[COF_FOG_PAYLOAD];
		int slot, section, wanted;
		qboolean moved;
		if( cl->state != cs_spawned || !SV_IsValidEdict( cl->edict ) || ( slot = CoF_FogSlot( cl->edict )) < 0 ) continue;
		section = SV_CoF_PlayerSection( cl->edict );
		moved = section >= 0 && cof_fog_players[slot].section != COF_FOG_UNKNOWN && section != cof_fog_players[slot].section;
		if( cof_fog_players[slot].replay || moved )
		{
			if( count < 0 ) count = CoF_FogGather( fogs, COF_FOG_MAX );
			wanted = CoF_FogWanted( fogs, count, section, payload );
			if( wanted || moved )
			{
				cof_fog_sending = true;
				SV_CoF_SendUserMessage( cl->edict, "SetFog", payload, sizeof( payload ), true );
				cof_fog_sending = false;
				Con_Printf( "cofstory phase=fog-sent userid=%d reason=%s on=%d\n", cl->userid, moved ? "section" : "reset", wanted );
			}
		}
		cof_fog_players[slot].replay = false;
		cof_fog_players[slot].section = section;
	}
}

void SV_CoF_FogReset( void )
{
	memset( cof_fog_players, 0, sizeof( cof_fog_players ));
	cof_fog_sending = false;
}
