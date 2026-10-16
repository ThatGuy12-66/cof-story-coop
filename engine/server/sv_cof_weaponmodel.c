/* Cry of Fear story co-op: the weapon other players see in a player's hands. GPL-3.0-or-later.
 * The rules are in sv_cof_weaponmodel.h; this file precaches the generated models
 * a copy has, works out each player's pick once a frame and puts it in the state
 * the player's entity is sent with. */
#include "common.h"
#include "server.h"
#include "sv_cof_story.h"
#include "sv_cof_retail.h"
#include "sv_cof_weaponmodel.h"

static struct
{
	int game; // the model index the game sends for this player's weapon
	int model; // what to send instead (0: the game's)
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
	// only the models this copy has: a client that lacks a precached model cannot join
	for( i = 0; CoF_WeaponModelPath( i ); i++ )
		if( FS_FileExists( CoF_WeaponModelPath( i ), false ))
		{
			SV_ModelIndex( CoF_WeaponModelPath( i ));
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
		char held[64];
		int right = 0, left = 0;
		const char *game, *pick;
		cof_weaponmodel_players[i].game = cof_weaponmodel_players[i].model = 0;
		if( cl->state != cs_spawned || !SV_IsValidEdict( cl->edict )) continue;
		if( !SV_CoF_RetailHeld( cl, held, sizeof( held ), &right, &left )) continue;
		game = cl->edict->v.weaponmodel ? SV_GetString( cl->edict->v.weaponmodel ) : "";
		pick = CoF_WeaponModelPick( game, held, right, left, CoF_WeaponModelPrecached, NULL );
		if( !pick ) continue;
		cof_weaponmodel_players[i].game = CoF_WeaponModelIndex( game );
		cof_weaponmodel_players[i].model = CoF_WeaponModelIndex( pick );
	}
}

void SV_CoF_WeaponModelState( entity_state_t *state, int e )
{
	int slot = e - 1;
	if( !state || slot < 0 || slot >= svs.maxclients || slot >= MAX_CLIENTS || !SV_CoF_StoryCoopEnabled( )) return;
	// only while the game still sends what the pick was made for (a switch this frame waits a frame)
	if( cof_weaponmodel_players[slot].model && state->weaponmodel == cof_weaponmodel_players[slot].game )
		state->weaponmodel = cof_weaponmodel_players[slot].model;
}
