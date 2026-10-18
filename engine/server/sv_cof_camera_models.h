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
// is gone or whose pDeathScene is no longer this model.
COF_CAMERA_MODELS_FN int CoF_CameraModelLeft( int owner_slot, int maxclients, int owner_present, const void *owner_scene, const void *model )
{
	if( owner_slot < 1 || owner_slot > maxclients ) return 0;
	return !owner_present || owner_scene != model;
}

#endif // SV_COF_CAMERA_MODELS_H
