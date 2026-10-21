/*
sv_cof_camera_models.c - Cry of Fear story co-op: the camera models a finished model cutscene leaves (sv_cof_camera_models.h)
GPL-3.0-or-later.

With cof_coop_camera_models (default 1), each frame a camera_model owned by a player slot whose player object is gone, or
no longer has it as pDeathScene, is removed as the game meant to. Log: phase=camera-model-removed entity owner.
1.1.9 (ladders): each frame, before the sends, the ladders' camera models (the climbers' first-person hands) are listed:
sv_frame.c sends one to its owner only, and sv_cof_weaponmodel.c draws the climber's body where those hands are.
*/
#include "common.h"
#include "server.h"
#include "sv_cof_story.h"
#include "sv_cof_cheats.h"
#include "sv_cof_camera_models.h"

static CVAR_DEFINE_AUTO( cof_coop_camera_models, "1", 0, "Cry of Fear story co-op: remove the camera models a finished model cutscene leaves behind; 0: they stay until the map changes, as in the game" );

void SV_CoF_CameraModelsInit( void )
{
	Cvar_RegisterVariable( &cof_coop_camera_models );
}

// 1.1.9 (ladders): the camera models showing a cof_ladder_manager's model (models/ladder_climb*.mdl) and owned by a
// player slot, this map (spawncount); climbing: the climb still holds its owner (CoF_CameraModelClimbing).
#define COF_CAMERA_LADDER_MODELS 16
#define COF_CAMERA_LADDERS 32
static struct { int entity, owner, modelindex; qboolean climbing; } cof_camera_ladders[COF_CAMERA_LADDERS];
static int cof_camera_ladder_count, cof_camera_ladder_spawn = -1;

// A precached model's index; never registers one.
static int CoF_CameraModelIndex( const char *model )
{
	char name[MAX_QPATH];
	int i;
	if( !model || !model[0] ) return 0;
	if( *model == '\\' || *model == '/' ) model++;
	Q_strncpy( name, model, sizeof( name ));
	COM_FixSlashes( name );
	for( i = 1; i < MAX_MODELS && sv.model_precache[i][0]; i++ )
		if( !Q_stricmp( sv.model_precache[i], name )) return i;
	return 0;
}

static void CoF_CameraModelsLadders( void )
{
	int models[COF_CAMERA_LADDER_MODELS], count = 0, i, j;
	cof_camera_ladder_count = 0;
	cof_camera_ladder_spawn = svs.spawncount;
	for( i = svs.maxclients + 1; i < svgame.numEntities; i++ )
	{
		edict_t *manager = SV_EdictNum( i );
		int index;
		if( !SV_IsValidEdict( manager ) || !manager->v.model || Q_strcmp( SV_GetString( manager->v.classname ), "cof_ladder_manager" )) continue;
		if(( index = CoF_CameraModelIndex( SV_GetString( manager->v.model ))) <= 0 ) continue;
		for( j = 0; j < count && models[j] != index; j++ );
		if( j == count && count < COF_CAMERA_LADDER_MODELS ) models[count++] = index;
	}
	for( i = svs.maxclients + 1; i < svgame.numEntities && count && cof_camera_ladder_count < COF_CAMERA_LADDERS; i++ )
	{
		edict_t *ent = SV_EdictNum( i ), *owner;
		byte *pv;
		void *scene = NULL;
		int slot;
		// a model the climb's end removed (FL_KILLME) stays hidden until it is gone
		if( !SV_IsValidEdict( ent ) || ent->v.modelindex <= 0 || Q_strcmp( SV_GetString( ent->v.classname ), "camera_model" )) continue;
		for( j = 0; j < count && models[j] != ent->v.modelindex; j++ );
		owner = ent->v.owner;
		slot = owner ? NUM_FOR_EDICT( owner ) : 0;
		if( j == count || slot < 1 || slot > svs.maxclients ) continue;
		if( SV_IsValidEdict( owner ) && owner->pvPrivateData && !Q_strcmp( SV_GetString( owner->v.classname ), "player" ) &&
			( pv = SV_CoF_StoryEntityPrivate( owner, COF_CAMERA_MODEL_SCENE + sizeof( void * ))) != NULL )
			memcpy( &scene, pv + COF_CAMERA_MODEL_SCENE, sizeof( scene ));
		cof_camera_ladders[cof_camera_ladder_count].entity = i;
		cof_camera_ladders[cof_camera_ladder_count].owner = slot;
		cof_camera_ladders[cof_camera_ladder_count].modelindex = ent->v.modelindex;
		cof_camera_ladders[cof_camera_ladder_count].climbing = !FBitSet( ent->v.flags, FL_KILLME ) &&
			CoF_CameraModelClimbing( 1, SV_IsValidEdict( owner ) && FBitSet( owner->v.flags, FL_FROZEN ), scene, ent->pvPrivateData );
		cof_camera_ladder_count++;
	}
}

// The ladder entry of this entity, while the list is this map's and the entity still the model it lists.
static int CoF_CameraModelsLadderOf( const edict_t *ent )
{
	int i, e;
	if( !cof_camera_ladder_count || cof_camera_ladder_spawn != svs.spawncount || !SV_IsValidEdict( ent )) return -1;
	e = NUM_FOR_EDICT( ent );
	for( i = 0; i < cof_camera_ladder_count; i++ )
		if( cof_camera_ladders[i].entity == e )
			return ent->v.modelindex == cof_camera_ladders[i].modelindex && ent->v.owner &&
				NUM_FOR_EDICT( ent->v.owner ) == cof_camera_ladders[i].owner ? i : -1;
	return -1;
}

qboolean SV_CoF_CameraModelHiddenFor( const edict_t *viewer, const edict_t *ent )
{
	int i;
	if( !viewer || !SV_CoF_StoryCoopEnabled( ) || ( i = CoF_CameraModelsLadderOf( ent )) < 0 ) return false;
	return CoF_CameraModelHidden( cof_camera_ladders[i].owner, svs.maxclients, 1, NUM_FOR_EDICT( viewer )) != 0;
}

// The ladder's hands model player entity e is climbing with, or NULL.
edict_t *SV_CoF_CameraModelLadder( int e )
{
	int i;
	if( !SV_CoF_StoryCoopEnabled( ) || cof_camera_ladder_spawn != svs.spawncount ) return NULL;
	for( i = 0; i < cof_camera_ladder_count; i++ )
	{
		edict_t *ent;
		if( cof_camera_ladders[i].owner != e || !cof_camera_ladders[i].climbing ) continue;
		ent = SV_EdictNum( cof_camera_ladders[i].entity );
		if( CoF_CameraModelsLadderOf( ent ) == i ) return ent;
	}
	return NULL;
}

void SV_CoF_CameraModelsFrame( void )
{
	int i;
	CoF_CameraModelsLadders( ); // 1.1.9: whatever cof_coop_camera_models says
	if( cof_coop_camera_models.value == 0.0f || !SV_CoF_StoryCoopEnabled( )) return;
	for( i = svs.maxclients + 1; i < svgame.numEntities; i++ )
	{
		edict_t *ent = SV_EdictNum( i ), *owner;
		byte *pv = NULL;
		void *scene = NULL;
		int slot, object = 0;
		if( !SV_IsValidEdict( ent ) || FBitSet( ent->v.flags, FL_KILLME ) || Q_strcmp( SV_GetString( ent->v.classname ), "camera_model" )) continue;
		owner = ent->v.owner;
		slot = owner ? NUM_FOR_EDICT( owner ) : 0;
		// 1.1.8 (E2-1): decide on the owner's player object, not on its client state. A player who left or is still
		// loading keeps that object (UTIL_PlayerByIndex returns it), a model cutscene gives it a model as its raw
		// pDeathScene and walks it at every step: removing that model left a dangling pointer.
		if( slot >= 1 && slot <= svs.maxclients && SV_IsValidEdict( owner ) && owner->pvPrivateData )
		{
			object = -1;
			if( !Q_strcmp( SV_GetString( owner->v.classname ), "player" ) &&
				( pv = SV_CoF_StoryEntityPrivate( owner, COF_CAMERA_MODEL_SCENE + sizeof( void * ))) != NULL )
			{
				memcpy( &scene, pv + COF_CAMERA_MODEL_SCENE, sizeof( scene ));
				object = 1;
			}
		}
		if( !CoF_CameraModelLeft( slot, svs.maxclients, object, scene, ent->pvPrivateData )) continue;
		SetBits( ent->v.flags, FL_KILLME );
		Con_Printf( "cofstory phase=camera-model-removed entity=%d owner=%d\n", i, slot );
	}
}
