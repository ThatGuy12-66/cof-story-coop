/*
sv_cof_camera_models.h - Cry of Fear story co-op: the camera models a finished model cutscene leaves (sv_cof_camera_models.c)
GPL-3.0-or-later.

A model cutscene (cof_mdlcutscene, the ladder climb, the strangle) creates one camera_model per player, owned by the
player, as the player's pDeathScene (CBasePlayer +0x5AC). At the end, cof_mdlcutscene's StartNextAnimation clears
pDeathScene and then removes it, i.e. removes NULL (hl.dll 1009F56E / 1009F5E9, 1009F6CC / 1009F747;
effects.cpp:9882, 9921 in the decompilation): the model, a sky entity sent to every client, stays until the map
changes. Research: docs/research/2026-10-18-cof-decompilation-survey.md section 3 (the launcher repo).

Plain C (standard library only): the offline tests share it.
*/
#ifndef SV_COF_CAMERA_MODELS_H
#define SV_COF_CAMERA_MODELS_H

#if defined( _MSC_VER )
#define COF_CAMERA_MODELS_FN static __inline
#else
#define COF_CAMERA_MODELS_FN static inline
#endif

#define COF_CAMERA_MODEL_SCENE 0x5AC // CBasePlayer::pDeathScene

// A camera_model the game is done with: owned by a player slot (1..maxclients; a map's own have no owner) whose player
// object is gone or whose pDeathScene is no longer this model. owner_object: 1 = the owner edict holds a player object
// we can read (owner_scene is its pDeathScene), 0 = it holds no object, -1 = an object we cannot read (kept: only that
// object could still point at the model). The client state does not count (1.1.8, E2-1): a player who left or is
// still loading keeps their object, and a running model cutscene walks its raw pDeathScene.
COF_CAMERA_MODELS_FN int CoF_CameraModelLeft( int owner_slot, int maxclients, int owner_object, const void *owner_scene, const void *model )
{
	if( owner_slot < 1 || owner_slot > maxclients || owner_object < 0 ) return 0;
	return !owner_object || owner_scene != model;
}

// 1.1.9 (ladders): a cof_ladder_manager's camera_model is the climber's first-person hands (CoF_Ladder.cpp:513-523),
// a plain entity sent to every client: the others saw hands float up the ladder. It is now sent to its owner only;
// the others see the climber's body on the ladder (sv_cof_weaponmodel.c). ladder: the model is a ladder manager's;
// viewer_slot: the viewer's entity number. The strangle's model (c_ending1c-3c) is not a ladder's and stays as it is.
COF_CAMERA_MODELS_FN int CoF_CameraModelHidden( int owner_slot, int maxclients, int ladder, int viewer_slot )
{
	return ladder && owner_slot >= 1 && owner_slot <= maxclients && viewer_slot != owner_slot;
}

// The climber's body follows the hands only while the climb holds them: frozen, and the model still their pDeathScene.
COF_CAMERA_MODELS_FN int CoF_CameraModelClimbing( int ladder, int owner_frozen, const void *owner_scene, const void *model )
{
	return ladder && owner_frozen && model && owner_scene == model;
}

#endif // SV_COF_CAMERA_MODELS_H
