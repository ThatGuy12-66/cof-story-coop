/* Cry of Fear story co-op: the weapon other players see in a player's hands. GPL-3.0-or-later.
 * The rules are in sv_cof_weaponmodel.h; this file precaches the generated models
 * a copy has, works out each player's pick once a frame, puts it in the state the
 * player's entity is sent with, and adds the other hand of a dual pair as a follow
 * entity (no edict: a number at the top of the edict range the game has not
 * reached, sent only to clients that see the player). */
#include "common.h"
#include "server.h"
#include "const.h"
#include "entity_state.h"
#include "sv_cof_story.h"
#include "sv_cof_retail.h"
#include "sv_cof_weaponmodel.h"

static struct
{
	int game; // the model index the game sends for this player's weapon
	int model; // what to send instead (0: the game's)
	int sequence; // the body sequence to send (-1: the game's)
	int offhand; // the other hand's model (0: none)
} cof_weaponmodel_players[MAX_CLIENTS];

// A precached model's index; never registers one.
static int CoF_WeaponModelIndex( const char *path )
{
	int i;
	if( !path || !path[0] ) return 0;
	for( i = 1; i < MAX_MODELS && sv.model_precache[i][0]; i++ )
		if( CoF_WeaponModelSame( sv.model_precache[i], path )) return i;
	return 0;
}

static int CoF_WeaponModelPrecached( const char *path, void *ctx )
{
	(void)ctx;
	return CoF_WeaponModelIndex( path ) > 0;
}

void SV_CoF_WeaponModelActivate( void )
{
	int i, n = 0;
	memset( cof_weaponmodel_players, 0, sizeof( cof_weaponmodel_players ));
	if( !SV_CoF_StoryCoopEnabled( )) return;
	// only the models this copy has, while the map loads: not fatal for a client
	// without one (it sees the game's own model), unlike the game's precache
	for( i = 0; CoF_WeaponModelPath( i ); i++ )
	{
		const char *path = CoF_WeaponModelPath( i );
		if( !path[0] || CoF_WeaponModelIndex( path ) || !FS_FileExists( path, false )) continue;
		SV_ModelIndex( path );
		n++;
	}
	Con_Printf( "cofstory phase=weapon-models precached=%d\n", n );
}

void SV_CoF_WeaponModelFrame( void )
{
	int i;
	for( i = 0; i < svs.maxclients && i < MAX_CLIENTS; i++ )
	{
		sv_client_t *cl = &svs.clients[i];
		cof_weaponmodel_pick_t pick;
		int id = 0, right = 0, left = 0;
		const char *game;
		memset( &cof_weaponmodel_players[i], 0, sizeof( cof_weaponmodel_players[i] ));
		cof_weaponmodel_players[i].sequence = COF_WEAPONMODEL_KEEP;
		if( cl->state != cs_spawned || !SV_IsValidEdict( cl->edict )) continue;
		if( !SV_CoF_RetailHeld( cl, &id, &right, &left )) continue;
		game = cl->edict->v.weaponmodel ? SV_GetString( cl->edict->v.weaponmodel ) : "";
		if( !CoF_WeaponModelPick( game, id, right, left, CoF_WeaponModelPrecached, NULL, &pick )) continue;
		cof_weaponmodel_players[i].game = CoF_WeaponModelIndex( game );
		cof_weaponmodel_players[i].model = CoF_WeaponModelIndex( pick.model );
		cof_weaponmodel_players[i].sequence = pick.sequence;
		cof_weaponmodel_players[i].offhand = CoF_WeaponModelIndex( pick.offhand );
	}
}

void SV_CoF_WeaponModelState( entity_state_t *state, int e )
{
	int slot = e - 1;
	if( !state || slot < 0 || slot >= svs.maxclients || slot >= MAX_CLIENTS || !SV_CoF_StoryCoopEnabled( )) return;
	// only while the game still sends what the pick was made for (a switch this frame waits a frame)
	if( state->weaponmodel != cof_weaponmodel_players[slot].game ) return;
	if( cof_weaponmodel_players[slot].model ) state->weaponmodel = cof_weaponmodel_players[slot].model;
	if( cof_weaponmodel_players[slot].sequence >= 0 && ( cof_weaponmodel_players[slot].model || cof_weaponmodel_players[slot].offhand ))
		state->sequence = cof_weaponmodel_players[slot].sequence;
}

int SV_CoF_WeaponModelFollower( const sv_client_t *viewer, int e, entity_state_t *out )
{
	int slot = e - 1, number;
	edict_t *player;
	if( !viewer || !out || slot < 0 || slot >= svs.maxclients || slot >= MAX_CLIENTS || !SV_CoF_StoryCoopEnabled( )) return 0;
	if( !cof_weaponmodel_players[slot].offhand ) return 0;
	// CoF hides every player while the viewer watches a camera: a follower would merge
	// onto whatever was drawn last
	if( viewer->pViewEntity && viewer->pViewEntity != viewer->edict ) return 0;
	player = svs.clients[slot].edict;
	if( !SV_IsValidEdict( player ) || FBitSet( player->v.effects, EF_NODRAW )) return 0;
	number = svgame.globals->maxEntities - 1 - slot;
	if( number < svgame.numEntities || number <= svs.maxclients ) return 0; // the game uses that many edicts
	memset( out, 0, sizeof( *out ));
	out->number = number;
	out->entityType = ENTITY_NORMAL;
	out->modelindex = cof_weaponmodel_players[slot].offhand;
	out->movetype = MOVETYPE_FOLLOW;
	out->aiment = e;
	out->solid = SOLID_NOT;
	VectorCopy( player->v.origin, out->origin );
	VectorCopy( player->v.angles, out->angles );
	out->rendermode = kRenderNormal;
	out->renderamt = 255;
	out->scale = 1.0f;
	out->animtime = (float)sv.time;
	return 1;
}
