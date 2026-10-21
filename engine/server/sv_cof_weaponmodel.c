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
#include "sv_cof_heldlight.h"

static CVAR_DEFINE_AUTO( cof_coop_heldlights, "1", 0, "Cry of Fear story co-op: a held lantern, lit flare or book laser lights the room for the other players too; 0: only for its holder, as the game does" );

static struct
{
	int game; // the model index the game sends for this player's weapon
	int model; // what to send instead (0: the game's)
	int sequence; // the body sequence to send (-1: the game's)
	int offhand; // the other hand's model (0: none)
	int light, renderamt, rgb[3]; // a held light the others see (sv_cof_heldlight.h)
	vec3_t light_origin;
} cof_weaponmodel_players[MAX_CLIENTS];

void SV_CoF_WeaponModelInit( void )
{
	Cvar_RegisterVariable( &cof_coop_heldlights );
}

// The light of what the player holds, for the others (sv_cof_heldlight.h).
static void CoF_WeaponModelHeldLight( sv_client_t *cl, int slot, int id, int right, int left )
{
	edict_t *ent = cl->edict;
	vec3_t forward, side, up;
	int flags = SV_CoF_RetailFlashFlags( cl );
	if( cof_coop_heldlights.value == 0.0f || flags < 0 || FBitSet( ent->v.effects, EF_NODRAW ) || ent->v.deadflag != DEAD_NO ) return;
	if( !( cof_weaponmodel_players[slot].light = CoF_HeldLight( id, right, left, flags ))) return;
	CoF_HeldLightLook( cof_weaponmodel_players[slot].light, COM_RandomLong( 0, 999 ), COM_RandomLong( 0, 999 ), COM_RandomLong( 0, 999 ),
		&cof_weaponmodel_players[slot].renderamt, cof_weaponmodel_players[slot].rgb );
	AngleVectors( ent->v.v_angle, forward, side, up );
	CoF_HeldLightOrigin( ent->v.origin, ent->v.view_ofs, forward, side, cof_weaponmodel_players[slot].light_origin );
}

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
		CoF_WeaponModelHeldLight( cl, i, id, right, left );
		game = cl->edict->v.weaponmodel ? SV_GetString( cl->edict->v.weaponmodel ) : "";
		if( !CoF_WeaponModelPick( game, id, right, left, CoF_WeaponModelPrecached, NULL, &pick )) continue;
		cof_weaponmodel_players[i].game = CoF_WeaponModelIndex( game );
		cof_weaponmodel_players[i].model = CoF_WeaponModelIndex( pick.model );
		if( CoF_WeaponModelStanding( cl->edict->v.deadflag, cl->edict->v.fuser3, FBitSet( cl->edict->v.flags, FL_DUCKING ),
			cl->edict->v.movetype == MOVETYPE_WALK, cl->edict->v.waterlevel, SV_CoF_RetailOwnAnimation( cl )))
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

// The light of a held lantern, flare or book laser of player entity e, for every viewer but its holder (whose client
// draws its own); a model-less renderfx 71 entity at the top of the edict range, below the followers. 0: none.
int SV_CoF_WeaponModelLight( const sv_client_t *viewer, int e, entity_state_t *out )
{
	int slot = e - 1, number;
	edict_t *player;
	if( !viewer || !out || slot < 0 || slot >= svs.maxclients || slot >= MAX_CLIENTS || !SV_CoF_StoryCoopEnabled( )) return 0;
	if( !cof_weaponmodel_players[slot].light || viewer == &svs.clients[slot] ) return 0;
	if( viewer->pViewEntity && viewer->pViewEntity != viewer->edict ) return 0; // as the followers: hidden in a camera
	player = svs.clients[slot].edict;
	if( !SV_IsValidEdict( player ) || !player->v.modelindex ) return 0;
	number = svgame.globals->maxEntities - 1 - svs.maxclients - slot;
	if( number < svgame.numEntities || number <= svs.maxclients ) return 0;
	memset( out, 0, sizeof( *out ));
	out->number = number;
	out->entityType = ENTITY_NORMAL;
	out->modelindex = player->v.modelindex; // never drawn: renderfx 71 is light only
	out->solid = SOLID_NOT;
	VectorCopy( cof_weaponmodel_players[slot].light_origin, out->origin );
	out->rendermode = kRenderNormal;
	out->renderfx = COF_HELDLIGHT_RENDERFX;
	out->renderamt = cof_weaponmodel_players[slot].renderamt;
	out->rendercolor.r = (byte)cof_weaponmodel_players[slot].rgb[0];
	out->rendercolor.g = (byte)cof_weaponmodel_players[slot].rgb[1];
	out->rendercolor.b = (byte)cof_weaponmodel_players[slot].rgb[2];
	out->scale = 0.0f; // not a spotlight
	out->animtime = (float)sv.time;
	return 1;
}
