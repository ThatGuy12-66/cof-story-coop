/*
sv_cof_camera_models.c - Cry of Fear story co-op: the camera models a finished model cutscene leaves (sv_cof_camera_models.h)
GPL-3.0-or-later.

With cof_coop_camera_models (default 1), each frame a camera_model owned by a player slot whose player object is gone, or
no longer has it as pDeathScene, is removed as the game meant to. Log: phase=camera-model-removed entity owner.
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

void SV_CoF_CameraModelsFrame( void )
{
	int i;
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
